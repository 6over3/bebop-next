import type { BebopCodec } from "../codec.js";

/** Converts generated values to and from the payload representation used by a channel. */
export interface BebopPayloadCodec<Payload> {
  encode<Value>(
    codec: BebopCodec<Value>,
    value: Value,
  ): Payload;

  decode<Value>(
    codec: BebopCodec<Value>,
    payload: Payload,
  ): Value | PromiseLike<Value>;
}
