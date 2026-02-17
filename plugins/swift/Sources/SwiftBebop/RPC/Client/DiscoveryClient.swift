/// Queries a server's available services via method ID 0.
public struct DiscoveryClient<C: BebopChannel>: Sendable {
  public let channel: C

  public init(channel: C) {
    self.channel = channel
  }

  /// List all services the server exposes.
  public func listServices(
    options: CallOptions = .init()
  ) async throws -> DiscoveryResponse {
    let data = try await channel.unary(method: BebopReservedMethod.discovery, request: [], options: options)
    return try DiscoveryResponse.decode(from: data)
  }
}
