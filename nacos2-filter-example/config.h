#pragma once

#include "envoy/server/filter_config.h"
#include "extensions/filters/http/common/empty_http_filter_config.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Nacos2FilterExample {

class Config : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig  {
public:
  Config() : Extensions::HttpFilters::Common::EmptyHttpFilterConfig("nacos2_example") {}

  Http::FilterFactoryCb createFilter(const std::string&, Server::Configuration::FactoryContext&) override;  
};
} // namespace Nacos2FilterExample
} // namespace Envoy
