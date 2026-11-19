import Foundation
import CoreBluetooth
import Combine

/// Lectura decodificada del paquete de 8 bytes del firmware (ESPECIFICACION_BLE.md).
struct LecturaBascula: Equatable {
    let version: UInt8
    let pesoDecigramos: Int32
    let estado: UInt8
    let bateriaMV: UInt16

    var peso: Double { Double(pesoDecigramos) / 10.0 }
    var estable: Bool { estado & 0x01 != 0 }
    var calibrada: Bool { estado & 0x02 != 0 }
    var bateriaBaja: Bool { estado & 0x04 != 0 }

    /// Decodifica 8 bytes little-endian.
    init?(datos: Data) {
        guard datos.count >= 8 else { return nil }
        let b = [UInt8](datos)
        version = b[0]
        let sin = UInt32(b[1]) | UInt32(b[2]) << 8 | UInt32(b[3]) << 16 | UInt32(b[4]) << 24
        pesoDecigramos = Int32(bitPattern: sin)
        estado = b[5]
        bateriaMV = UInt16(b[6]) | UInt16(b[7]) << 8
    }
}

final class BasculaBLE: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    static let servicioUUID    = CBUUID(string: "6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
    static let lecturaUUID     = CBUUID(string: "6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
    static let comandoUUID     = CBUUID(string: "6E400003-B5A3-F393-E0A9-E50E24DCCA9E")
    static let respuestaUUID   = CBUUID(string: "6E400004-B5A3-F393-E0A9-E50E24DCCA9E")
    static let nombreAnuncio   = "NutriGo-Bascula"

    @Published private(set) var lectura: LecturaBascula?
    @Published private(set) var estado: String = "Iniciando Bluetooth…"
    @Published private(set) var conectada = false
    @Published private(set) var ultimaRespuesta: String = ""

    private var central: CBCentralManager!
    private var periferico: CBPeripheral?
    private var caracteristicaComando: CBCharacteristic?

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    // MARK: - Comandos hacia la báscula

    func tara() { enviar(0x01) }

    /// Calibra con un peso patrón en gramos (se envía como int32 en décimas de gramo).
    func calibrar(gramos: Double) {
        let dg = Int32((gramos * 10).rounded())
        let bytes = withUnsafeBytes(of: dg.littleEndian) { Array($0) }
        enviar(0x02, payload: bytes)
    }

    func dormir() { enviar(0x03) }

    private func enviar(_ comando: UInt8, payload: [UInt8] = []) {
        guard let p = periferico, let c = caracteristicaComando else {
            estado = "No conectada: no se puede enviar el comando"
            return
        }
        p.writeValue(Data([comando] + payload), for: c, type: .withoutResponse)
    }

    // MARK: - CBCentralManagerDelegate

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            buscar()
        case .poweredOff:
            estado = "Bluetooth apagado"
        case .unauthorized:
            estado = "Sin permiso de Bluetooth (revisa Ajustes del Sistema)"
        default:
            estado = "Bluetooth no disponible"
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        central.stopScan()
        periferico = peripheral
        peripheral.delegate = self
        estado = "Conectando…"
        central.connect(peripheral, options: nil)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        conectada = true
        estado = "Conectada"
        peripheral.discoverServices([Self.servicioUUID])
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral,
                        error: Error?) {
        conectada = false
        caracteristicaComando = nil
        estado = "Desconectada, buscando de nuevo…"
        buscar()
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        estado = "No pude conectar, reintentando…"
        buscar()
    }

    // MARK: - CBPeripheralDelegate

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let servicio = peripheral.services?.first(where: { $0.uuid == Self.servicioUUID }) else { return }
        peripheral.discoverCharacteristics([Self.lecturaUUID, Self.comandoUUID, Self.respuestaUUID],
                                           for: servicio)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        for c in service.characteristics ?? [] {
            switch c.uuid {
            case Self.lecturaUUID, Self.respuestaUUID:
                peripheral.setNotifyValue(true, for: c)
            case Self.comandoUUID:
                caracteristicaComando = c
            default:
                break
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        guard let datos = characteristic.value else { return }
        switch characteristic.uuid {
        case Self.lecturaUUID:
            if let l = LecturaBascula(datos: datos) { lectura = l }
        case Self.respuestaUUID:
            let b = [UInt8](datos)
            guard b.count >= 2 else { return }
            let ok = b[1] == 0x00
            ultimaRespuesta = "Comando \(String(format: "0x%02X", b[0])): \(ok ? "ok" : "error")"
        default:
            break
        }
    }

    // MARK: - Privados

    private func buscar() {
        guard central.state == .poweredOn else { return }
        estado = "Buscando \(Self.nombreAnuncio)…"
        central.scanForPeripherals(withServices: [Self.servicioUUID], options: nil)
    }
}
