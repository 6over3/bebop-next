/// A Bebop type that exposes compile-time reflection metadata.
///
/// Generated types conform to this protocol. The metadata includes the
/// type's name, fully-qualified name, definition kind, and field/member details.
public protocol BebopReflectable {
    static var bebopReflection: BebopTypeReflection { get }
}

/// Compile-time metadata for a generated Bebop type.
public struct BebopTypeReflection: Sendable {
    public let name: String
    public let fqn: String
    public let kind: BebopDefinitionKind
    public let detail: Detail

    public enum Detail: Sendable {
        case `struct`(StructReflection)
        case message(MessageReflection)
        case `enum`(EnumReflection)
        case union(UnionReflection)
    }

    public init(name: String, fqn: String, kind: BebopDefinitionKind, detail: Detail) {
        self.name = name
        self.fqn = fqn
        self.kind = kind
        self.detail = detail
    }
}

/// Metadata for a single field in a struct or message.
public struct BebopFieldReflection: Sendable {
    public let name: String
    public let index: UInt32
    public let typeName: String

    public init(name: String, index: UInt32, typeName: String) {
        self.name = name
        self.index = index
        self.typeName = typeName
    }
}

public struct StructReflection: Sendable {
    public let fields: [BebopFieldReflection]

    public init(fields: [BebopFieldReflection]) {
        self.fields = fields
    }
}

public struct MessageReflection: Sendable {
    public let fields: [BebopFieldReflection]

    public init(fields: [BebopFieldReflection]) {
        self.fields = fields
    }
}

public struct EnumReflection: Sendable {
    public let members: [(name: String, value: UInt64)]
    public let isFlags: Bool

    public init(members: [(name: String, value: UInt64)], isFlags: Bool) {
        self.members = members
        self.isFlags = isFlags
    }
}

public struct UnionReflection: Sendable {
    public let branches: [(discriminator: UInt8, name: String, typeName: String)]

    public init(branches: [(discriminator: UInt8, name: String, typeName: String)]) {
        self.branches = branches
    }
}

/// The kind of Bebop type definition.
public enum BebopDefinitionKind: Sendable {
    case `struct`, message, `enum`, union
}
