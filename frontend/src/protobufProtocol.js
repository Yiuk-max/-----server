import protobuf from 'protobufjs'
import schema from '../../src/proto/message.proto?raw'

const { root } = protobuf.parse(schema, { keepCase: true })
const Envelope = root.lookupType('chat_proto.Envelope')
const HEADER_BYTES = 4

function asBytes(data) {
  if (data instanceof ArrayBuffer) return new Uint8Array(data)
  if (ArrayBuffer.isView(data)) return new Uint8Array(data.buffer, data.byteOffset, data.byteLength)
  return null
}

export function encodePacket(value, fileData = null) {
  const error = Envelope.verify(value)
  if (error) throw new Error(`Invalid Envelope: ${error}`)
  const body = Envelope.encode(Envelope.fromObject(value)).finish()
  const tail = fileData ? asBytes(fileData) : null
  if (fileData && !tail) throw new Error('fileData must be ArrayBuffer or a typed array')
  const frame = new Uint8Array(HEADER_BYTES + body.length + (tail?.byteLength || 0))
  new DataView(frame.buffer).setUint32(0, body.length, false)
  frame.set(body, HEADER_BYTES)
  if (tail) frame.set(tail, HEADER_BYTES + body.length)
  return frame
}

export async function decodePacket(data) {
  let bytes = asBytes(data)
  if (!bytes && data instanceof Blob) bytes = new Uint8Array(await data.arrayBuffer())
  if (!bytes || bytes.length < HEADER_BYTES) throw new Error('Invalid binary frame')
  const payloadLength = new DataView(bytes.buffer, bytes.byteOffset, HEADER_BYTES).getUint32(0, false)
  if (payloadLength > bytes.length - HEADER_BYTES) throw new Error('Protobuf payload length exceeds frame size')
  const message = Envelope.decode(bytes.subarray(HEADER_BYTES, HEADER_BYTES + payloadLength))
  return {
    message: Envelope.toObject(message, { defaults: false, arrays: true, longs: Number, enums: String }),
    fileData: bytes.subarray(HEADER_BYTES + payloadLength),
  }
}
