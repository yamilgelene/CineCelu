import SwiftUI

@main
struct CineCeluApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
                .frame(minWidth: 760, minHeight: 520)
        }
        .defaultSize(width: 860, height: 620)
        .windowResizability(.contentMinSize)
    }
}
