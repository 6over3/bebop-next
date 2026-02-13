import Foundation
import SwiftBebop

extension BebopUUID {
    /// Create from a Foundation `UUID`. Zero-cost — both types share the same 16-byte tuple layout.
    @inlinable
    public init(_ uuid: UUID) {
        self.init(uuid: uuid.uuid)
    }
}

extension UUID {
    /// Create from a `BebopUUID`. Zero-cost — both types share the same 16-byte tuple layout.
    @inlinable
    public init(_ bebopUUID: BebopUUID) {
        self.init(uuid: bebopUUID.uuid)
    }
}
