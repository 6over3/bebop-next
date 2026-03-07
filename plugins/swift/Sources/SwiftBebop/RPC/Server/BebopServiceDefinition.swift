/// Generated per-service enum conforming to this ties method lookup
/// and service metadata together for router registration.
public protocol BebopServiceDefinition: Sendable {
    associatedtype Method: BebopServiceMethod

    static var serviceName: String { get }
    static var serviceInfo: ServiceInfo { get }
    static func method(for id: UInt32) -> Method?
}
