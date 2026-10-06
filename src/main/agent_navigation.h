#ifndef ISLAND_AGENT_NAVIGATION_H_
#define ISLAND_AGENT_NAVIGATION_H_

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "address_policy.h"

namespace island {

using AddressValidator = std::function<ValidatedAddress(std::string_view)>;

// Turns what an agent asked to open into a URL the address policy accepts,
// trying in order: the text as typed; "https://" + text when it looks like a
// bare host ("example.com/docs"); otherwise a Google search for the text.
// Returns std::nullopt for blank input. Every candidate goes through
// `validate`, so agents can never reach a URL the address bar would reject.
[[nodiscard]] std::optional<std::string> ResolveAgentNavigation(std::string_view input,
                                                                const AddressValidator& validate);

}  // namespace island

#endif  // ISLAND_AGENT_NAVIGATION_H_
