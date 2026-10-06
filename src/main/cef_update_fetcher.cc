#include "cef_update_fetcher.h"

#include <string>
#include <utility>

#include "include/cef_request.h"
#include "include/cef_response.h"
#include "include/cef_urlrequest.h"
#include "include/wrapper/cef_helpers.h"
#include "island_version.h"

namespace island {

class CefUpdateFetcher::Client final : public CefURLRequestClient {
  public:
    Client(update::FetchRequest request, Progress progress, Done done)
        : request_(std::move(request)), progress_(std::move(progress)), done_(std::move(done)) {}

    void Begin() { Load(request_.url); }

    void Cancel() {
        cancelled_ = true;
        if (url_request_ != nullptr) {
            url_request_->Cancel();
            url_request_ = nullptr;
        }
        // A finished download belongs to the caller now; only partial files
        // are removed.
        if (!finished_) {
            sink_.Discard();
        }
    }

    void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
        CEF_REQUIRE_UI_THREAD();
        if (cancelled_ || request != url_request_) {
            return;
        }
        url_request_ = nullptr;
        CefRefPtr<CefResponse> response = request->GetResponse();
        const int http_status = response != nullptr ? response->GetStatus() : 0;
        if (http_status == 301 || http_status == 302 || http_status == 303 || http_status == 307 ||
            http_status == 308) {
            const std::optional<std::string> next = update::ResolveRedirect(
                current_url_, response->GetHeaderByName("Location").ToString());
            if (!next.has_value()) {
                Fail(http_status, "redirected to a location outside GitHub");
                return;
            }
            if (++redirects_ > update::kMaxRedirects) {
                Fail(http_status, "too many redirects");
                return;
            }
            Load(*next);
            return;
        }
        if (sink_.overflowed()) {
            Fail(http_status, "the response is larger than expected");
            return;
        }
        if (http_status < 200 || http_status > 299) {
            if (http_status != 0) {
                Fail(http_status, "GitHub answered HTTP " + std::to_string(http_status));
                return;
            }
            Fail(0, request->GetRequestStatus() == UR_CANCELED
                        ? "the request was cancelled"
                        : "network error " +
                              std::to_string(static_cast<int>(request->GetRequestError())));
            return;
        }
        if (request->GetRequestStatus() != UR_SUCCESS) {
            Fail(http_status, "the download was interrupted");
            return;
        }
        update::FetchResult result;
        if (!sink_.Close(&result.sha256_hex)) {
            sink_.Discard();
            Fail(http_status, "couldn't write the download to disk");
            return;
        }
        result.ok = true;
        result.http_status = http_status;
        result.size = sink_.size();
        result.body = sink_.TakeBody();
        Finish(std::move(result));
    }

    void OnUploadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}

    void OnDownloadProgress(CefRefPtr<CefURLRequest> request, int64_t current,
                            int64_t total) override {
        CEF_REQUIRE_UI_THREAD();
        if (cancelled_ || request != url_request_ || !progress_ || !IsSuccess(request)) {
            return;
        }
        progress_(current, total);
    }

    void OnDownloadData(CefRefPtr<CefURLRequest> request, const void* data,
                        size_t data_length) override {
        CEF_REQUIRE_UI_THREAD();
        // Only the final 2xx body is kept; redirect and error bodies are
        // dropped.
        if (cancelled_ || request != url_request_ || !IsSuccess(request)) {
            return;
        }
        if (!sink_.Append(data, data_length)) {
            request->Cancel();
        }
    }

    bool GetAuthCredentials(bool, const CefString&, int, const CefString&, const CefString&,
                            CefRefPtr<CefAuthCallback>) override {
        return false;  // never answer auth challenges
    }

  private:
    static bool IsSuccess(const CefRefPtr<CefURLRequest>& request) {
        CefRefPtr<CefResponse> response = request->GetResponse();
        const int status = response != nullptr ? response->GetStatus() : 0;
        return status >= 200 && status <= 299;
    }

    void Load(const std::string& url) {
        if (!update::IsAllowedUpdateUrl(url)) {
            Fail(0, "refused to fetch a URL outside GitHub");
            return;
        }
        current_url_ = url;
        // Each hop starts a fresh body; the sink truncates the file.
        sink_.Discard();
        if (!sink_.Open(request_.destination, request_.max_bytes)) {
            Fail(0, "couldn't create the download file");
            return;
        }
        CefRefPtr<CefRequest> request = CefRequest::Create();
        if (request == nullptr) {
            Fail(0, "couldn't create the request");
            return;
        }
        request->SetURL(url);
        request->SetMethod("GET");
        CefRequest::HeaderMap headers;
        headers.emplace("User-Agent", "Island/" ISLAND_VERSION_STRING);
        headers.emplace("Accept", request_.accept.empty() ? std::string("*/*") : request_.accept);
        if (url.rfind("https://api.github.com/", 0) == 0) {
            headers.emplace("X-GitHub-Api-Version", "2022-11-28");
        }
        request->SetHeaderMap(headers);
        // No cookies or stored credentials (the default without
        // UR_FLAG_ALLOW_STORED_CREDENTIALS), no cache, manual redirects.
        request->SetFlags(UR_FLAG_SKIP_CACHE | UR_FLAG_STOP_ON_REDIRECT);
        url_request_ = CefURLRequest::Create(request, this, nullptr);
        if (url_request_ == nullptr) {
            Fail(0, "couldn't start the request");
        }
    }

    void Fail(int http_status, std::string error) {
        sink_.Discard();
        update::FetchResult result;
        result.http_status = http_status;
        result.error = std::move(error);
        Finish(std::move(result));
    }

    void Finish(update::FetchResult result) {
        if (cancelled_ || !done_) {
            return;
        }
        finished_ = true;
        Done done = std::move(done_);
        done_ = nullptr;
        done(std::move(result));
    }

    update::FetchRequest request_;
    Progress progress_;
    Done done_;
    update::DownloadSink sink_;
    CefRefPtr<CefURLRequest> url_request_;
    std::string current_url_;
    int redirects_ = 0;
    bool cancelled_ = false;
    bool finished_ = false;

    IMPLEMENT_REFCOUNTING(Client);
    DISALLOW_COPY_AND_ASSIGN(Client);
};

CefUpdateFetcher::CefUpdateFetcher() = default;

CefUpdateFetcher::~CefUpdateFetcher() { Cancel(); }

void CefUpdateFetcher::Start(update::FetchRequest request, Progress progress, Done done) {
    CEF_REQUIRE_UI_THREAD();
    Cancel();
    // The client is kept alive across Begin(): a synchronous failure may run
    // |done|, which can start the next request and replace client_.
    CefRefPtr<Client> client = new Client(std::move(request), std::move(progress), std::move(done));
    client_ = client;
    client->Begin();
}

void CefUpdateFetcher::Cancel() {
    if (client_ != nullptr) {
        client_->Cancel();
        client_ = nullptr;
    }
}

}  // namespace island
