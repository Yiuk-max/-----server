import protobuf from 'protobufjs';
import schema from '../../src/proto/message.proto?raw';

const { root } = protobuf.parse(schema, { keepCase: true });
const Envelope = root.lookupType('chat_proto.Envelope');
const HEADER_BYTES = 4;

function asBytes(data) {
  if (data instanceof ArrayBuffer) return new Uint8Array(data);
  if (ArrayBuffer.isView(data)) {
    return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
  }
  return null;
}

export function encodeEnvelope(value) {
  const error = Envelope.verify(value);
  if (error) throw new Error(`Invalid Envelope: ${error}`);

  const body = Envelope.encode(Envelope.fromObject(value)).finish();
  const frame = new Uint8Array(HEADER_BYTES + body.length);
  new DataView(frame.buffer).setUint32(0, body.length, false);
  frame.set(body, HEADER_BYTES);
  return frame;
}

export async function decodeEnvelope(data) {
  let bytes = asBytes(data);
  if (!bytes && data instanceof Blob) {
    bytes = new Uint8Array(await data.arrayBuffer());
  }
  if (!bytes) throw new Error('WebSocket message is not binary data');
  if (bytes.length < HEADER_BYTES) throw new Error('Binary frame is smaller than its 4-byte header');

  const payloadLength = new DataView(bytes.buffer, bytes.byteOffset, HEADER_BYTES)
    .getUint32(0, false);
  if (payloadLength > bytes.length - HEADER_BYTES) {
    throw new Error(`Protobuf payload length ${payloadLength} exceeds frame size ${bytes.length}`);
  }

  const payload = bytes.subarray(HEADER_BYTES, HEADER_BYTES + payloadLength);
  const message = Envelope.decode(payload);
  return Envelope.toObject(message, {
    defaults: false,
    arrays: true,
    longs: Number,
    enums: String,
  });
}
