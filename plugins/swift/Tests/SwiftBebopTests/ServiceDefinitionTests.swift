import Testing

@testable import SwiftBebop

@Suite struct ServiceDefinitionTests {
  @Test func serviceName() {
    #expect(WidgetService.serviceName == "WidgetService")
  }

  @Test func serviceInfoMethods() {
    let info = WidgetService.serviceInfo
    #expect(info.name == "WidgetService")
    #expect(info.methods.count == 4)
    #expect(info.methods[0].name == "GetWidget")
    #expect(info.methods[0].methodType == .unary)
    #expect(info.methods[1].name == "ListWidgets")
    #expect(info.methods[1].methodType == .serverStream)
    #expect(info.methods[2].name == "UploadWidgets")
    #expect(info.methods[2].methodType == .clientStream)
    #expect(info.methods[3].name == "SyncWidgets")
    #expect(info.methods[3].methodType == .duplexStream)
  }

  @Test func methodLookup() {
    #expect(WidgetService.method(for: getWidgetId) == .getWidget)
    #expect(WidgetService.method(for: listWidgetsId) == .listWidgets)
    #expect(WidgetService.method(for: uploadWidgetsId) == .uploadWidgets)
    #expect(WidgetService.method(for: syncWidgetsId) == .syncWidgets)
    #expect(WidgetService.method(for: 0xFFFF) == nil)
  }

  @Test func methodProperties() {
    let m = WidgetService.Method.getWidget
    #expect(m.rawValue == getWidgetId)
    #expect(m.name == "GetWidget")
    #expect(m.methodType == .unary)
    #expect(m.requestTypeUrl == "type.bebop.sh/EchoRequest")
    #expect(m.responseTypeUrl == "type.bebop.sh/EchoResponse")
  }

  @Test func allCases() {
    #expect(WidgetService.Method.allCases.count == 4)
  }
}
