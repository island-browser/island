#ifndef ISLAND_CEF_UPDATE_FETCHER_H_
#define ISLAND_CEF_UPDATE_FETCHER_H_

// The updater's network glue: update::UpdateFetcher over CefURLRequest in the
// browser process. Requests carry `User-Agent: Island/<version>`, never send
// cookies or credentials, and only reach URLs update::IsAllowedUpdateUrl
// accepts. Redirects are followed manually (UR_FLAG_STOP_ON_REDIRECT), so
// every hop is checked against the same allow-list — GitHub asset downloads
// redirect to *.githubusercontent.com. Bodies stream through a size-capped,
// hashing update::DownloadSink. Create and use it on the UI thread; its
// callbacks arrive there too.

#include "include/cef_base.h"
#include "updater.h"

namespace island {

class CefUpdateFetcher final : public update::UpdateFetcher {
  public:
    CefUpdateFetcher();
    ~CefUpdateFetcher() override;

    void Start(update::FetchRequest request, Progress progress, Done done) override;
    void Cancel() override;

    class Client;

  private:
    CefRefPtr<Client> client_;
};

}  // namespace island

#endif  // ISLAND_CEF_UPDATE_FETCHER_H_
