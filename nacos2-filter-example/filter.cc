#include "filter.h"

#include "common/common/enum_to_int.h"
#include "common/grpc/codec.h"
#include "common/grpc/common.h"
#include "common/grpc/status.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
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

Filter::Filter(Grpc::Context& context): context_(context) {
  ENVOY_LOG(info, "construct nacos2 filter");
}

Filter::~Filter() {
  ENVOY_LOG(info, "destroy nacos2 filter");
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  ENVOY_LOG(info, "nacos2 request headers complete (end_stream={}): {} filter", end_stream, headers);
  // Short circuit if header only.
  if (end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }

  // TODO(snowp): Add an enabled flag so that this filter can be enabled on a per route basis.

  // If this is a gRPC request we:
  //  - mark this request as being gRPC
  //  - change the content-type to application/x-protobuf
  if (Envoy::Grpc::Common::hasGrpcContentType(headers)) {
    nacos2_enabled_ = true;
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& buffer, bool end_stream) {
  ENVOY_LOG(info, "nacos request complete end_stream: {}", end_stream);
  if (nacos2_enabled_) {
    // Fail the request if the body is too small to possibly contain a gRPC frame.
    if (buffer.length() < Grpc::GRPC_FRAME_HEADER_SIZE) {
      decoder_callbacks_->sendLocalReply(Http::Code::OK, "invalid request body", nullptr,
                                         Grpc::Status::GrpcStatus::Unknown,
                                         RcDetails::get().GrpcBridgeFailedTooSmall);
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
    // For requests, EOS (end-of-stream) is indicated by the presence of the END_STREAM flag on the last received DATA frame. 
    // In scenarios where the Request stream needs to be closed but no data remains to be sent implementations MUST send an empty DATA frame with this flag set.
    if (end_stream) {
      ENVOY_LOG(info, "nacos2 request complete");
      com::alibaba::nacos::api::grpc::Payload nacos_playload;
      onDecodeComplete(buffer, &nacos_playload);
    }
  } else {
    ENVOY_LOG(info, "nacos1 request complete {}", buffer.toString());
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::HeaderMap& trailers) {
    ENVOY_LOG(info, "nacos request trailers {}", trailers);
    return Http::FilterTrailersStatus::Continue;
}

bool Filter::onDecodeComplete(Buffer::Instance& data, com::alibaba::nacos::api::grpc::Payload* playload) {
  ENVOY_LOG(info, "nacos2  DATA frame complete length {} \n", data.length());
  // 不能改这个 data，否则继续 continue 时会导致 upstream 解析这个 grpc 请求报错，nacos-server 会直接 reset 
  std::string body =  data.toString().substr(Grpc::GRPC_FRAME_HEADER_SIZE);
  ENVOY_LOG(info, "nacos2 playload length {} \n", body.length());
  if (playload->ParseFromString(body)) {
    ENVOY_LOG(info, "nacos2 palyload {} \n", playload->DebugString());
    return true;
  } else {
    ENVOY_LOG(error, "nacos2 playload parse fail.");
  }

  return false;
}
// *********************************
// for response

/*
   The HEADERS frame defines the following flags:

   END_STREAM (0x1):  When set, bit 0 indicates that the header block
      (Section 4.3) is the last that the endpoint will send for the
      identified stream.

      A HEADERS frame carries the END_STREAM flag that signals the end
      of a stream.  However, a HEADERS frame with the END_STREAM flag
      set can be followed by CONTINUATION frames on the same stream.
      Logically, the CONTINUATION frames are part of the HEADERS frame.

   END_HEADERS (0x4):  When set, bit 2 indicates that this frame
      contains an entire header block (Section 4.3) and is not followed
      by any CONTINUATION frames.

      A HEADERS frame without the END_HEADERS flag set MUST be followed
      by a CONTINUATION frame for the same stream.  A receiver MUST
      treat the receipt of any other type of frame or a frame on a
      different stream as a connection error (Section 5.4.1) of type
      PROTOCOL_ERROR.

   PADDED (0x8):  When set, bit 3 indicates that the Pad Length field
      and any padding that it describes are present.

   PRIORITY (0x20):  When set, bit 5 indicates that the Exclusive Flag
      (E), Stream Dependency, and Weight fields are present; see
      Section 5.3.

   The payload of a HEADERS frame contains a header block fragment
   (Section 4.3).  A header block that does not fit within a HEADERS
   frame is continued in a CONTINUATION frame (Section 6.10).

   HEADERS frames MUST be associated with a stream.  If a HEADERS frame
   is received whose stream identifier field is 0x0, the recipient MUST
   respond with a connection error (Section 5.4.1) of type
   PROTOCOL_ERROR.

   The HEADERS frame changes the connection state as described in
   Section 4.3.

   The HEADERS frame can include padding.  Padding fields and flags are
   identical to those defined for DATA frames (Section 6.1).  Padding
   that exceeds the size remaining for the header block fragment MUST be
   treated as a PROTOCOL_ERROR.

   Prioritization information in a HEADERS frame is logically equivalent
   to a separate PRIORITY frame, but inclusion in HEADERS avoids the
   potential for churn in stream prioritization when new streams are
   created.  Prioritization fields in HEADERS frames subsequent to the
   first on a stream reprioritize the stream (Section 5.3.3).
*/
Http::FilterHeadersStatus Filter::encodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  ENVOY_LOG(info, "nacos response headers complete (end_stream={}): \n{}", end_stream, headers);
  if (Envoy::Grpc::Common::hasGrpcContentType(headers)) {
    nacos2_enabled_ = true;
  }

  return Http::FilterHeadersStatus::Continue;
}

/*
The DATA frame defines the following flags:
   END_STREAM (0x1):  When set, bit 0 indicates that this frame is the
      last that the endpoint will send for the identified stream.
      Setting this flag causes the stream to enter one of the "half-
      closed" states or the "closed" state (Section 5.1).

   PADDED (0x8):  When set, bit 3 indicates that the Pad Length field
      and any padding that it describes are present.

   DATA frames MUST be associated with a stream.  If a DATA frame is
   received whose stream identifier field is 0x0, the recipient MUST
   respond with a connection error (Section 5.4.1) of type
   PROTOCOL_ERROR.

   DATA frames are subject to flow control and can only be sent when a
   stream is in the "open" or "half-closed (remote)" state.  The entire
   DATA frame payload is included in flow control, including the Pad
   Length and Padding fields if present.  If a DATA frame is received
   whose stream is not in "open" or "half-closed (local)" state, the
   recipient MUST respond with a stream error (Section 5.4.2) of type
   STREAM_CLOSED.

   The total number of padding octets is determined by the value of the
   Pad Length field.  If the length of the padding is the length of the
   frame payload or greater, the recipient MUST treat this as a
   connection error (Section 5.4.1) of type PROTOCOL_ERROR.

      Note: A frame can be increased in size by one octet by including a
      Pad Length field with a value of zero.
*/

Http::FilterDataStatus Filter::encodeData(Buffer::Instance& buffer, bool end_stream) {
  // 在 response 的 DATA frame 里面 end_stream 就没为空过
  ENVOY_LOG(info, "nacos2 response end_stream: {} filter", end_stream);
  if (nacos2_enabled_) {
    // Fail the request if the body is too small to possibly contain a gRPC frame.
    if (buffer.length() < Grpc::GRPC_FRAME_HEADER_SIZE) {
      decoder_callbacks_->sendLocalReply(Http::Code::OK, "invalid response body", nullptr,
                                         Grpc::Status::GrpcStatus::Unknown,
                                         RcDetails::get().GrpcBridgeFailedTooSmall);
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
    com::alibaba::nacos::api::grpc::Payload nacos_playload;
    if (onDecodeComplete(buffer, &nacos_playload)) {
      // 如果是服务发现相关的请求
      ::com::alibaba::nacos::api::grpc::Metadata nacos_metadata = nacos_playload.metadata();
      if ( nacos_metadata.type() == "SubscribeServiceResponse" || nacos_metadata.type() == "NotifySubscriberRequest") {
          rapidjson::Document document;
          rapidjson::ParseResult ok = document.Parse(nacos_playload.body().value().c_str());
          if (!ok) {
            ENVOY_LOG(error, "nacos2 json body {} parse fail", nacos_playload.body().value());
          }
          rapidjson::Value& hosts =  document["serviceInfo"]["hosts"];
          for (rapidjson::SizeType i = 0; i < hosts.Size(); i++) {
            ENVOY_LOG(info, "nacos2 return host ip {}", hosts[i]["ip"].GetString());
            // 替换 ip 字段为服务名
            std::string service_name = document["serviceInfo"]["name"].GetString();
            rapidjson::Value& host_ip = hosts[i]["ip"];
            host_ip.SetString(service_name.c_str(), service_name.size());
          }
          rapidjson::StringBuffer json_buffer;
          rapidjson::Writer<rapidjson::StringBuffer> writer(json_buffer);
          document.Accept(writer);
          ENVOY_LOG(info, "nacos2 {} replace host ip res {}", nacos_metadata.type(), json_buffer.GetString());

          // 构造新的 pb 作为 response 的 playload
          nacos_playload.mutable_body()->set_value(std::string(json_buffer.GetString()));
          ENVOY_LOG(info, "nacos2 replace host ip new_palyload {}", nacos_playload.DebugString());

          std::string new_playload;
          nacos_playload.SerializeToString(&new_playload);

          Buffer::OwnedImpl new_buffer{};
          new_buffer.prepend(new_playload);
          const auto length = nacos_playload.ByteSize();
          std::array<uint8_t, Grpc::GRPC_FRAME_HEADER_SIZE> frame;
          Grpc::Encoder().newFrame(Grpc::GRPC_FH_DEFAULT, length, frame);
          Buffer::OwnedImpl frame_buffer(frame.data(), frame.size());
          new_buffer.prepend(frame_buffer);
          ENVOY_LOG(info, "nacos2 response DATA frame length {}, new_buffer lenght {}\n", buffer.length(), new_buffer.length());
          // 这样写，buffer 好像没改变？
          // buffer = new_buffer;

          // 试试这样
          buffer.drain(buffer.length());
          buffer.move(new_buffer);
      }
    }
  } else {
    ENVOY_LOG(info, "nacos1 response complete {}", buffer.toString());
  }
  return Http::FilterDataStatus::Continue;
}

// For responses end-of-stream is indicated by the presence of the END_STREAM flag on the last received HEADERS frame that carries Trailers.
// 从抓包来看 grpc 的回应包，在 DATA frame 之后都会有个 HEADERS frame，其中有会设置 END_STREAM，同时带上trailer header
Http::FilterTrailersStatus Filter::encodeTrailers(Http::HeaderMap& trailers) {
    ENVOY_LOG(info, "nacos response trailers \n {}", trailers);
    return Http::FilterTrailersStatus::Continue;
}

Http::FilterMetadataStatus Filter::encodeMetadata(Http::MetadataMap& metadata) {
    ENVOY_LOG(info, "nacos response metadata \n {}", metadata);
    return Http::FilterMetadataStatus::Continue;
}

} // namespace GrpcHttp1ReverseBridge
} // namespace Envoy
