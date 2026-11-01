#ifndef ISLAND_MAIN_APP_RUNTIME_H_
#define ISLAND_MAIN_APP_RUNTIME_H_

#include <optional>

#include "include/cef_app.h"

namespace island {

class IslandApp;

class IslandAppReleaseObserver {
  public:
    virtual ~IslandAppReleaseObserver() = default;
    virtual void OnBeforeIslandAppRelease() = 0;
};

int RunIslandMainProcess(const CefMainArgs& main_args, CefRefPtr<IslandApp>&& app,
                         IslandAppReleaseObserver* release_observer, void* sandbox_info,
                         std::optional<int> remote_debugging_port);

}  // namespace island

#endif  // ISLAND_MAIN_APP_RUNTIME_H_
