import Foundation
import SwiftBebop

public extension BebopUUID {
    /// Create from a Foundation `UUID`. Zero-cost — both types share the same 16-byte tuple layout.
    @inlinable
    init(_ uuid: UUID) {
        self.init(uuid: uuid.uuid)
    }
}

public extension UUID {
    /// Create from a `BebopUUID`. Zero-cost — both types share the same 16-byte tuple layout.
    @inlinable
    init(_ bebopUUID: BebopUUID) {
        self.init(uuid: bebopUUID.uuid)
    }
}
