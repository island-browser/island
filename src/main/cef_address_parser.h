#ifndef ISLAND_CEF_ADDRESS_PARSER_H_
#define ISLAND_CEF_ADDRESS_PARSER_H_

#include <string_view>

#include "address_policy.h"

namespace island {

[[nodiscard]] ValidatedAddress ParseAndValidate(std::string_view input);

}  // namespace island

#endif
