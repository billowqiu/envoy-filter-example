#pragma once

#include <string>

#include "envoy/http/filter.h"
#include "common/common/logger.h"
#include "common/buffer/buffer_impl.h"
#include "common/grpc/status.h"
#include "common/grpc/context_impl.h"
#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Nacos2FilterExample {

// When enabled, will downgrade an incoming gRPC http request into a h/1.1 request.
class Filter : public Envoy::Http::PassThroughFilter, Logger::Loggable<Logger::Id::filter> {
public:
  explicit Filter(Grpc::Context& context) : context_(context) {}
  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& buffer, bool end_stream) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& buffer, bool end_stream) override;

private:
  void onDecodeComplete(Buffer::Instance& data);
private:
  bool enabled_{};
  Grpc::Status::GrpcStatus grpc_status_{};
  // Normally we'd use the encoding buffer, but since we need to mutate the
  // buffer we instead maintain our own.
  Buffer::OwnedImpl buffer_{};
  Grpc::Context& context_;
};
} // namespace Nacos2FilterExample
} // namespace Envoy
