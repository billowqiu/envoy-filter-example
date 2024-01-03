#include "filter.h"

#include "common/common/enum_to_int.h"
#include "common/grpc/codec.h"
#include "common/grpc/common.h"
#include "common/grpc/status.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "nacos2-filter-example/nacos_grpc_service.pb.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Nacos2FilterExample {

struct RcDetailsValues {
  // The gRPC HTTP/1 reverse bridge failed because the body payload was too
  // small to be a gRPC frame.
  const std::string GrpcBridgeFailedTooSmall = "grpc_bridge_data_too_small";
  // The gRPC HTTP/1 bridge encountered an unsupported content type.
  const std::string GrpcBridgeFailedContentType = "grpc_bridge_content_type_wrong";
};
using RcDetails = ConstSingleton<RcDetailsValues>;

namespace {
Grpc::Status::GrpcStatus grpcStatusFromHeaders(Http::HeaderMap& headers) {
  const auto http_response_status = Http::Utility::getResponseStatus(headers);

  // Notably, we treat an upstream 200 as a successful response. This differs
  // from the standard but is key in being able to transform a successful
  // upstream HTTP response into a gRPC response.
  if (http_response_status == 200) {
    return Grpc::Status::GrpcStatus::Ok;
  } else {
    return Grpc::Utility::httpToGrpcStatus(http_response_status);
  }
}

void adjustContentLength(Http::HeaderMap& headers,
                         std::function<uint64_t(uint64_t value)> adjustment) {
  auto length_header = headers.ContentLength();
  if (length_header != nullptr) {
    uint64_t length;
    if (absl::SimpleAtoi(length_header->value().getStringView(), &length)) {
      length_header->value(adjustment(length));
    }
  }
}
} // namespace

Http::FilterHeadersStatus Filter::decodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  // Short circuit if header only.
  if (end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }

  // TODO(snowp): Add an enabled flag so that this filter can be enabled on a per route basis.

  // If this is a gRPC request we:
  //  - mark this request as being gRPC
  //  - change the content-type to application/x-protobuf
  if (Envoy::Grpc::Common::hasGrpcContentType(headers)) {
    enabled_ = true;
  }

  // ENVOY_CONN_LOG(trace, "nacos2 request headers complete (end_stream={}):\n{}", context_. end_stream, headers);
  // ENVOY_STREAM_LOG(debug, "nacos2 request headers complete (end_stream={}):\n{}", end_stream, headers);
  ENVOY_LOG(info, "==> nacos2 request headers complete (end_stream={}):\n{}", end_stream, headers);

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& buffer, bool end_stream) {
  if (enabled_) {
    // Fail the request if the body is too small to possibly contain a gRPC frame.
    if (buffer.length() < Grpc::GRPC_FRAME_HEADER_SIZE) {
      decoder_callbacks_->sendLocalReply(Http::Code::OK, "invalid request body", nullptr,
                                         Grpc::Status::GrpcStatus::Unknown,
                                         RcDetails::get().GrpcBridgeFailedTooSmall);
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
      if (end_stream) {
        onDecodeComplete(buffer);
      }
  }
  return Http::FilterDataStatus::Continue;
}

void Filter::onDecodeComplete(Buffer::Instance& data) {
  ENVOY_LOG(info, "==> nacos2 request data complete length {} \n", data.length());
  // 不能改这个 data，否则继续 continue 时会导致 upstream 解析这个 grpc 请求报错，nacos-server 会直接 reset
  // data.drain(Grpc::GRPC_FRAME_HEADER_SIZE); 
  std::string body =  data.toString().substr(5);
  ENVOY_LOG(info, "==> nacos2 request data to string length {} \n", body.length());
  com::alibaba::nacos::api::grpc::Payload nacos_playload;
  if (nacos_playload.ParseFromString(body)) {
    ENVOY_LOG(info, "==> nacos2 request data to palyload {} \n", nacos_playload.DebugString());
  } else {
    ENVOY_LOG(warn, "==> nacos2 request data to palyload fail\n");
  }
}

// for response

Http::FilterHeadersStatus Filter::encodeHeaders(Http::HeaderMap& headers, bool) {
  if (enabled_) {
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance& buffer, bool end_stream) {
  if (!enabled_) {
    return Http::FilterDataStatus::Continue;
  }

  if (end_stream) {
    return Http::FilterDataStatus::Continue;
  }
}

} // namespace GrpcHttp1ReverseBridge
} // namespace Envoy
