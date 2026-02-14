/// Generated method enum whose raw value is the MurmurHash3 method ID.
public protocol BebopServiceMethod: RawRepresentable<UInt32>,
  CaseIterable, Hashable, Sendable
{
  var name: String { get }
  var methodType: MethodType { get }
  var requestTypeUrl: String { get }
  var responseTypeUrl: String { get }
}
