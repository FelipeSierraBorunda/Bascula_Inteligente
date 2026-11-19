# Crear la app en Xcode (macOS)

1. Xcode → File → New → Project → macOS → App. Interface: SwiftUI, Language: Swift.
2. Borra `ContentView.swift` y `NutriGoApp.swift` generados y arrastra los tres `.swift` de esta carpeta al proyecto (marca "Copy items if needed").
3. Target → Info → agrega la clave `NSBluetoothAlwaysUsageDescription` con el texto "Conectar con la báscula NutriGo".
4. Target → Signing & Capabilities → App Sandbox → marca **Bluetooth** (en Hardware).
5. Cmd+R para ejecutar. La primera vez macOS pedirá permiso de Bluetooth.

Para iPhone más adelante: agrega el target iOS y usa la misma `NSBluetoothAlwaysUsageDescription`.
