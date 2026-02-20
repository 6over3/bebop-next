/// Queries a server's available services via method ID 0.
public struct DiscoveryClient<C: BebopChannel>: Sendable {
  public let channel: C

  public init(channel: C) {
    self.channel = channel
  }

  public func listServices(
    context: RpcContext = RpcContext()
  ) async throws -> DiscoveryResponse {
    let result = try await channel.unary(
      method: BebopReservedMethod.discovery, request: [], context: context)
    return try DiscoveryResponse.decode(from: result.value)
  }
}
