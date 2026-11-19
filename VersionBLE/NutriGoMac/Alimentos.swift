import SwiftUI
#if os(iOS)
import UIKit
#else
import AppKit
#endif

/// Alimento (valores por 100 g, o por 100 ml en bebidas). Son valores aproximados de referencia.
struct Alimento: Identifiable, Hashable, Codable {
    var id: String { nombre }
    let nombre: String
    var categoria: String = "Otros"
    let kcal: Double
    let proteina: Double
    let carbos: Double
    let grasa: Double
}

struct ErrorAlimento: Error { let mensaje: String }

// MARK: - Portapapeles

enum Portapapeles {
    static func copiar(_ texto: String) {
#if os(iOS)
        UIPasteboard.general.string = texto
#else
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(texto, forType: .string)
#endif
    }

    static func leer() -> String? {
#if os(iOS)
        return UIPasteboard.general.string
#else
        return NSPasteboard.general.string(forType: .string)
#endif
    }
}

// MARK: - Alimentos propios (guardados en el dispositivo)

final class AlimentosStore: ObservableObject {
    @Published private(set) var propios: [Alimento] = []
    private let clave = "alimentosPersonalizados"

    /// Texto que se copia para pedirle a una IA el alimento en el formato que entiende la app.
    static let formatoParaIA = """
    Eres un asistente de nutrición. Responde ÚNICAMENTE con un JSON válido, sin texto extra ni markdown, con este formato exacto. \
    Los valores son aproximados por 100 g (por 100 ml si es bebida). "categoria" debe ser una de: \
    Platillos, Antojitos, Panadería, Botanas, Bebidas, Dulces, Lácteos, Frutas, Verduras, Carnes, Cereales, Otros.

    {"nombre":"<NOMBRE>","categoria":"<CATEGORIA>","kcal":0,"proteina":0,"carbos":0,"grasa":0}

    Si te doy varios alimentos, responde con una lista JSON de esos objetos.

    Alimento: [ESCRIBE AQUÍ EL ALIMENTO]
    """

    init() {
        if let data = UserDefaults.standard.data(forKey: clave),
           let lista = try? JSONDecoder().decode([Alimento].self, from: data) {
            propios = lista
        }
    }

    var todos: [Alimento] { propios + alimentosBase }

    /// Interpreta el texto pegado (JSON de un alimento o lista). Devuelve cuántos agregó o un error.
    func agregar(desdeTexto texto: String) -> Result<Int, ErrorAlimento> {
        guard let json = extraerJSON(texto),
              let data = json.data(using: .utf8),
              let obj = try? JSONSerialization.jsonObject(with: data) else {
            return .failure(ErrorAlimento(mensaje: "No encontré un JSON en el portapapeles."))
        }
        let items: [[String: Any]]
        if let d = obj as? [String: Any] { items = [d] }
        else if let l = obj as? [[String: Any]] { items = l }
        else { return .failure(ErrorAlimento(mensaje: "El formato no es válido.")) }

        var nuevos: [Alimento] = []
        for d in items {
            guard let nombre = (d["nombre"] as? String)?.trimmingCharacters(in: .whitespacesAndNewlines),
                  !nombre.isEmpty,
                  let kcal = numero(d["kcal"]) else { continue }
            nuevos.append(Alimento(
                nombre: nombre,
                categoria: "Mis alimentos",
                kcal: max(0, kcal),
                proteina: max(0, numero(d["proteina"]) ?? 0),
                carbos: max(0, numero(d["carbos"]) ?? 0),
                grasa: max(0, numero(d["grasa"]) ?? 0)))
        }
        guard !nuevos.isEmpty else { return .failure(ErrorAlimento(mensaje: "Faltan campos (nombre y kcal son obligatorios).")) }

        for n in nuevos {
            propios.removeAll { $0.nombre.caseInsensitiveCompare(n.nombre) == .orderedSame }
            propios.insert(n, at: 0)
        }
        guardar()
        return .success(nuevos.count)
    }

    func quitar(_ a: Alimento) {
        propios.removeAll { $0.nombre == a.nombre }
        guardar()
    }

    private func guardar() {
        if let data = try? JSONEncoder().encode(propios) {
            UserDefaults.standard.set(data, forKey: clave)
        }
    }

    private func numero(_ v: Any?) -> Double? {
        if let n = v as? Double { return n }
        if let s = v as? String { return Double(s.replacingOccurrences(of: ",", with: ".")) }
        return nil
    }

    /// Toma el primer bloque `{...}` o `[...]` del texto (ignora ```json y texto alrededor).
    private func extraerJSON(_ t: String) -> String? {
        let limpio = t.replacingOccurrences(of: "```json", with: "").replacingOccurrences(of: "```", with: "")
        let a = limpio.firstIndex(where: { $0 == "{" || $0 == "[" })
        let b = limpio.lastIndex(where: { $0 == "}" || $0 == "]" })
        guard let a, let b, a <= b else { return nil }
        return String(limpio[a...b])
    }
}

// MARK: - Base de alimentos

private func a(_ n: String, _ c: String, _ k: Double, _ p: Double, _ ch: Double, _ g: Double) -> Alimento {
    Alimento(nombre: n, categoria: c, kcal: k, proteina: p, carbos: ch, grasa: g)
}

let alimentosBase: [Alimento] = [
    // Gorditas de nata y dulces locales
    a("Gordita de nata", "Gorditas de nata", 380, 6, 45, 19),
    a("Gordita de nata con Nutella", "Gorditas de nata", 430, 6, 52, 22),
    a("Gordita de nata sin Nutella (simple)", "Gorditas de nata", 380, 6, 45, 19),
    a("Gordita de nata con cajeta", "Gorditas de nata", 410, 6, 56, 18),
    a("Gordita de nata con queso de bola", "Gorditas de nata", 400, 10, 40, 22),
    a("Marquesita", "Gorditas de nata", 330, 6, 48, 13),
    a("Marquesita con Nutella", "Gorditas de nata", 400, 6, 55, 18),
    a("Dulce de papaya", "Gorditas de nata", 280, 1, 70, 0.3),
    a("Dulce de tamarindo", "Gorditas de nata", 280, 2, 70, 1),
    a("Churros", "Gorditas de nata", 380, 5, 45, 20),

    // Platillos yucatecos
    a("Cochinita pibil", "Platillos yucatecos", 190, 17, 2, 12),
    a("Pollo pibil", "Platillos yucatecos", 180, 22, 3, 9),
    a("Poc chuc", "Platillos yucatecos", 210, 22, 1, 13),
    a("Relleno negro", "Platillos yucatecos", 200, 15, 5, 13),
    a("Relleno blanco (pavo)", "Platillos yucatecos", 160, 18, 4, 8),
    a("Queso relleno", "Platillos yucatecos", 230, 13, 6, 17),
    a("Frijol con puerco", "Platillos yucatecos", 130, 8, 12, 5),
    a("Sopa de lima", "Platillos yucatecos", 60, 5, 6, 2),
    a("Longaniza de Valladolid", "Platillos yucatecos", 330, 15, 2, 29),
    a("Lomitos de Valladolid", "Platillos yucatecos", 150, 13, 5, 9),
    a("Escabeche oriental", "Platillos yucatecos", 130, 14, 3, 7),
    a("Dzotobichay", "Platillos yucatecos", 170, 8, 18, 7),

    // Antojitos
    a("Panucho", "Antojitos", 210, 9, 22, 9),
    a("Salbute", "Antojitos", 220, 8, 20, 12),
    a("Papadzules", "Antojitos", 180, 7, 15, 11),
    a("Tamal colado", "Antojitos", 160, 6, 20, 6),
    a("Tamal de puerco", "Antojitos", 210, 8, 22, 10),
    a("Taco de cochinita", "Antojitos", 200, 11, 18, 9),
    a("Taco al pastor", "Antojitos", 220, 12, 20, 10),
    a("Taco de canasta", "Antojitos", 180, 6, 24, 7),
    a("Quesadilla de queso", "Antojitos", 280, 13, 25, 14),
    a("Tostada de pollo", "Antojitos", 200, 12, 18, 9),
    a("Enchiladas verdes", "Antojitos", 150, 9, 14, 7),
    a("Chilaquiles", "Antojitos", 180, 7, 18, 9),
    a("Pozole", "Antojitos", 90, 8, 8, 3),
    a("Elote en vaso (esquites)", "Antojitos", 150, 4, 20, 7),
    a("Torta de jamón", "Antojitos", 250, 11, 30, 9),
    a("Torta de cochinita", "Antojitos", 230, 12, 27, 8),
    a("Burrito de frijol", "Antojitos", 190, 7, 30, 5),
    a("Mangonada", "Antojitos", 70, 0.5, 18, 0.1),
    a("Raspado de jarabe", "Antojitos", 70, 0, 18, 0),

    // Comida rápida
    a("Hamburguesa", "Comida rápida", 250, 13, 24, 11),
    a("Big Mac (McDonald's)", "Comida rápida", 257, 12, 20, 14),
    a("McNuggets (McDonald's)", "Comida rápida", 296, 15, 16, 19),
    a("Whopper (Burger King)", "Comida rápida", 240, 12, 17, 13),
    a("Pollo frito (KFC)", "Comida rápida", 260, 19, 10, 16),
    a("Pizza de pepperoni", "Comida rápida", 270, 12, 30, 11),
    a("Hot dog", "Comida rápida", 260, 10, 22, 15),
    a("Papas a la francesa", "Comida rápida", 312, 3.4, 41, 15),
    a("Sándwich de pavo (Subway)", "Comida rápida", 130, 8, 18, 2.5),
    a("Nuggets de pollo", "Comida rápida", 290, 15, 18, 18),

    // Bimbo, Marinela y panadería
    a("Pan blanco Bimbo", "Panadería y Bimbo", 265, 8, 50, 3.5),
    a("Pan integral Bimbo", "Panadería y Bimbo", 240, 10, 42, 3.5),
    a("Mantecadas Bimbo", "Panadería y Bimbo", 400, 5, 55, 18),
    a("Roles de canela Bimbo", "Panadería y Bimbo", 400, 6, 60, 15),
    a("Donitas azucaradas Bimbo", "Panadería y Bimbo", 440, 5, 55, 22),
    a("Gansito Marinela", "Panadería y Bimbo", 417, 5, 55, 19),
    a("Pingüinos Marinela", "Panadería y Bimbo", 380, 5, 53, 16),
    a("Submarinos Marinela", "Panadería y Bimbo", 400, 5, 55, 18),
    a("Barritas Marinela", "Panadería y Bimbo", 400, 5, 62, 15),
    a("Bolillo", "Panadería y Bimbo", 280, 9, 56, 1),
    a("Concha", "Panadería y Bimbo", 380, 7, 56, 14),
    a("Pan de elote", "Panadería y Bimbo", 250, 5, 38, 9),
    a("Tortilla de maíz", "Panadería y Bimbo", 218, 5.7, 44, 2.9),
    a("Tortilla de harina", "Panadería y Bimbo", 310, 8, 50, 8),
    a("Tostada horneada", "Panadería y Bimbo", 360, 8, 70, 4),

    // Botanas y galletas
    a("Takis Fuego", "Botanas y galletas", 500, 6, 57, 28),
    a("Sabritas Original", "Botanas y galletas", 540, 6, 50, 35),
    a("Ruffles", "Botanas y galletas", 540, 6, 50, 35),
    a("Doritos Nacho", "Botanas y galletas", 500, 7, 60, 25),
    a("Cheetos Torciditos", "Botanas y galletas", 520, 6, 55, 30),
    a("Rancheritos", "Botanas y galletas", 520, 5, 55, 30),
    a("Tostitos", "Botanas y galletas", 480, 6, 62, 22),
    a("Cacahuates japoneses", "Botanas y galletas", 480, 13, 50, 26),
    a("Chicharrón de cerdo", "Botanas y galletas", 544, 61, 0, 31),
    a("Palomitas naturales", "Botanas y galletas", 387, 13, 78, 4.5),
    a("Chokis Gamesa", "Botanas y galletas", 480, 5, 68, 21),
    a("Emperador chocolate", "Botanas y galletas", 470, 5, 68, 20),
    a("Oreo", "Botanas y galletas", 480, 4.5, 70, 20),
    a("Marías Gamesa", "Botanas y galletas", 430, 8, 76, 10),
    a("Saladitas", "Botanas y galletas", 440, 9, 70, 14),
    a("Galleta de avena", "Botanas y galletas", 450, 6, 68, 18),
    a("Sopa instantánea Maruchan (seca)", "Botanas y galletas", 445, 9, 60, 19),

    // Dulces y chocolates
    a("Nutella", "Dulces y chocolates", 539, 6.3, 57.5, 30.9),
    a("Chocolate Carlos V", "Dulces y chocolates", 530, 6, 58, 31),
    a("Snickers", "Dulces y chocolates", 490, 9, 60, 24),
    a("Hershey's barra", "Dulces y chocolates", 535, 7.5, 59, 30),
    a("Kinder Bueno", "Dulces y chocolates", 575, 9, 50, 37),
    a("Ferrero Rocher", "Dulces y chocolates", 570, 8, 45, 40),
    a("Mazapán De la Rosa", "Dulces y chocolates", 500, 13, 50, 28),
    a("Pulparindo", "Dulces y chocolates", 300, 1, 70, 2),
    a("Gomitas", "Dulces y chocolates", 340, 6, 77, 0.1),
    a("Helado de vainilla", "Dulces y chocolates", 207, 3.5, 24, 11),
    a("Helado de coco", "Dulces y chocolates", 190, 2.5, 24, 10),
    a("Cajeta", "Dulces y chocolates", 320, 6, 60, 7),
    a("Mermelada", "Dulces y chocolates", 250, 0.4, 62, 0),
    a("Miel", "Dulces y chocolates", 304, 0.3, 82, 0),
    a("Mantequilla de cacahuate", "Dulces y chocolates", 588, 25, 20, 50),

    // Bebidas (por 100 ml)
    a("Coca-Cola (refresco)", "Bebidas", 42, 0, 10.6, 0),
    a("Coca-Cola Zero", "Bebidas", 0.3, 0, 0, 0),
    a("Pepsi", "Bebidas", 41, 0, 11, 0),
    a("Sprite", "Bebidas", 38, 0, 9.4, 0),
    a("Fanta naranja", "Bebidas", 40, 0, 10, 0),
    a("Sidral Mundet", "Bebidas", 45, 0, 11, 0),
    a("Jarritos", "Bebidas", 40, 0, 10, 0),
    a("Boing de mango", "Bebidas", 45, 0, 11, 0),
    a("Jugo del Valle", "Bebidas", 45, 0, 11, 0),
    a("Néctar Jumex", "Bebidas", 50, 0, 12, 0),
    a("Jugo de naranja natural", "Bebidas", 45, 0.7, 10, 0.2),
    a("Red Bull", "Bebidas", 45, 0, 11, 0),
    a("Monster", "Bebidas", 46, 0, 11, 0),
    a("Gatorade", "Bebidas", 26, 0, 6.2, 0),
    a("Powerade", "Bebidas", 25, 0, 6, 0),
    a("Yakult", "Bebidas", 76, 1, 17, 0),
    a("Chocomilk", "Bebidas", 80, 3, 14, 1.5),
    a("Leche entera", "Bebidas", 61, 3.2, 4.8, 3.3),
    a("Leche descremada", "Bebidas", 35, 3.4, 5, 0.1),
    a("Agua de horchata", "Bebidas", 55, 0.5, 12, 0.5),
    a("Agua de jamaica", "Bebidas", 25, 0, 6, 0),
    a("Atole de maíz", "Bebidas", 55, 1, 11, 1),
    a("Cerveza", "Bebidas", 43, 0.5, 3.6, 0),
    a("Café americano", "Bebidas", 1, 0.1, 0, 0),
    a("Agua natural", "Bebidas", 0, 0, 0, 0),

    // Lácteos y huevo
    a("Huevo entero", "Lácteos y huevo", 143, 12.6, 0.7, 9.5),
    a("Queso Oaxaca", "Lácteos y huevo", 300, 22, 2, 23),
    a("Queso panela", "Lácteos y huevo", 250, 20, 3, 17),
    a("Queso manchego", "Lácteos y huevo", 380, 25, 2, 30),
    a("Queso de bola", "Lácteos y huevo", 350, 25, 1, 28),
    a("Crema", "Lácteos y huevo", 200, 2.5, 4, 20),
    a("Yogurt natural", "Lácteos y huevo", 61, 3.5, 4.7, 3.3),
    a("Yogurt de fresa", "Lácteos y huevo", 90, 3, 15, 2),

    // Carnes y proteínas
    a("Pechuga de pollo cocida", "Carnes y proteínas", 165, 31, 0, 3.6),
    a("Pierna de pollo cocida", "Carnes y proteínas", 210, 26, 0, 11),
    a("Bistec de res", "Carnes y proteínas", 200, 27, 0, 10),
    a("Arrachera", "Carnes y proteínas", 200, 25, 0, 11),
    a("Carne molida de res", "Carnes y proteínas", 250, 17, 0, 20),
    a("Carnitas", "Carnes y proteínas", 250, 25, 0, 17),
    a("Costilla de cerdo", "Carnes y proteínas", 290, 24, 0, 21),
    a("Tocino", "Carnes y proteínas", 540, 37, 1.4, 42),
    a("Chorizo", "Carnes y proteínas", 450, 24, 2, 38),
    a("Jamón de pavo", "Carnes y proteínas", 110, 17, 2, 3.5),
    a("Salchicha", "Carnes y proteínas", 250, 11, 4, 21),
    a("Atún en agua", "Carnes y proteínas", 116, 26, 0, 1),
    a("Sardina", "Carnes y proteínas", 208, 25, 0, 11),
    a("Pescado a la plancha", "Carnes y proteínas", 130, 24, 0, 3),
    a("Salmón", "Carnes y proteínas", 208, 22, 0, 13),
    a("Camarón cocido", "Carnes y proteínas", 99, 24, 0.2, 0.3),

    // Cereales, granos y guarniciones
    a("Arroz blanco cocido", "Cereales y granos", 130, 2.7, 28, 0.3),
    a("Frijoles de olla", "Cereales y granos", 77, 5, 14, 0.5),
    a("Frijoles refritos", "Cereales y granos", 100, 5, 14, 3),
    a("Pasta cocida", "Cereales y granos", 158, 6, 31, 0.9),
    a("Papa cocida", "Cereales y granos", 87, 2, 20, 0.1),
    a("Camote", "Cereales y granos", 90, 2, 21, 0.2),
    a("Elote cocido", "Cereales y granos", 96, 3.4, 21, 1.5),
    a("Avena en hojuelas", "Cereales y granos", 389, 17, 66, 7),
    a("Granola", "Cereales y granos", 450, 10, 64, 17),
    a("Zucaritas", "Cereales y granos", 380, 5, 86, 1),
    a("Choco Krispis", "Cereales y granos", 390, 5, 83, 3.5),
    a("Corn Flakes", "Cereales y granos", 375, 7, 84, 0.9),
    a("Froot Loops", "Cereales y granos", 380, 6, 84, 3),

    // Frutas
    a("Papaya", "Frutas", 43, 0.5, 11, 0.3),
    a("Mamey", "Frutas", 124, 1.4, 32, 0.5),
    a("Plátano", "Frutas", 89, 1.1, 23, 0.3),
    a("Manzana", "Frutas", 52, 0.3, 14, 0.2),
    a("Naranja", "Frutas", 47, 0.9, 12, 0.1),
    a("Mango", "Frutas", 60, 0.8, 15, 0.4),
    a("Sandía", "Frutas", 30, 0.6, 8, 0.2),
    a("Melón", "Frutas", 34, 0.8, 8, 0.2),
    a("Piña", "Frutas", 50, 0.5, 13, 0.1),
    a("Uvas", "Frutas", 69, 0.7, 18, 0.2),
    a("Fresa", "Frutas", 32, 0.7, 8, 0.3),
    a("Aguacate", "Frutas", 160, 2, 9, 15),
    a("Limón", "Frutas", 29, 1.1, 9, 0.3),

    // Verduras y semillas
    a("Jícama", "Verduras y semillas", 38, 0.7, 9, 0.1),
    a("Pepino", "Verduras y semillas", 15, 0.7, 3.6, 0.1),
    a("Zanahoria", "Verduras y semillas", 41, 0.9, 10, 0.2),
    a("Jitomate", "Verduras y semillas", 18, 0.9, 3.9, 0.2),
    a("Lechuga", "Verduras y semillas", 15, 1.4, 2.9, 0.2),
    a("Cebolla", "Verduras y semillas", 40, 1.1, 9, 0.1),
    a("Brócoli", "Verduras y semillas", 34, 2.8, 7, 0.4),
    a("Nopal", "Verduras y semillas", 16, 1.3, 3.3, 0.1),
    a("Chile jalapeño", "Verduras y semillas", 29, 0.9, 6.5, 0.4),
    a("Pepita de calabaza", "Verduras y semillas", 559, 30, 11, 49),
    a("Cacahuate", "Verduras y semillas", 567, 26, 16, 49),
    a("Almendra", "Verduras y semillas", 579, 21, 22, 50),
    a("Nuez", "Verduras y semillas", 654, 15, 14, 65),
    a("Chía", "Verduras y semillas", 486, 17, 42, 31),

    // Aderezos
    a("Mayonesa", "Aderezos", 680, 1, 3, 75),
    a("Kétchup", "Aderezos", 110, 1, 26, 0.1),
    a("Salsa Valentina", "Aderezos", 20, 1, 4, 0),
]
