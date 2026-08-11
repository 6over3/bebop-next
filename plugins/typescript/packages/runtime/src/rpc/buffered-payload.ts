import { decode, encode } from "../codec.js";
import type { BebopPayloadCodec } from "./payload.js";

/** The payload codec for channels that exchange complete messages in memory. */
export const bufferedPayload = {
  encode(codec, value) {
    return encode(codec, value);
  },
  decode(codec, payload) {
    return decode(codec, payload);
  },
} satisfies BebopPayloadCodec<Uint8Array>;
