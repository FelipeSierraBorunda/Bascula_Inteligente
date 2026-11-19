import SwiftUI

@main
struct NutriGoApp: App {
    @StateObject private var bascula = BasculaBLE()
    @StateObject private var store = AlimentosStore()

    var body: some Scene {
        WindowGroup {
            ContentView(bascula: bascula, store: store)
#if os(macOS)
                .frame(minWidth: 440, minHeight: 640)
#endif
        }
    }
}
