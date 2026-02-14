extension StatusCode: CustomStringConvertible {
  public var name: String? {
    switch self {
    case .ok: "OK"
    case .cancelled: "CANCELLED"
    case .unknown: "UNKNOWN"
    case .invalidArgument: "INVALID_ARGUMENT"
    case .deadlineExceeded: "DEADLINE_EXCEEDED"
    case .notFound: "NOT_FOUND"
    case .permissionDenied: "PERMISSION_DENIED"
    case .resourceExhausted: "RESOURCE_EXHAUSTED"
    case .unimplemented: "UNIMPLEMENTED"
    case .internal: "INTERNAL"
    case .unavailable: "UNAVAILABLE"
    case .unauthenticated: "UNAUTHENTICATED"
    default: nil
    }
  }

  public var description: String {
    name ?? "STATUS_\(rawValue)"
  }
}
