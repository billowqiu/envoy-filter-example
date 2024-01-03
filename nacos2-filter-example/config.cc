#include "config.h"
#include "envoy/registry/registry.h"
#include "filter.h"

namespace Envoy {
namespace Nacos2FilterExample {

Http::FilterFactoryCb
Config::createFilter(const std::string&, Server::Configuration::FactoryContext& factory_context) {
  return [&factory_context](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamFilter(std::make_shared<Filter>(factory_context.grpcContext()));
  };
}

REGISTER_FACTORY(Config, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Nacos2FilterExample
} // namespace Envoy
