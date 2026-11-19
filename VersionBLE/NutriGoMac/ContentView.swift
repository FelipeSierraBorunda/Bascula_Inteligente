import SwiftUI

// MARK: - Tema

private enum Tema {
    static let fondoArriba = Color(red: 0.05, green: 0.04, blue: 0.16)
    static let fondoAbajo = Color(red: 0.16, green: 0.04, blue: 0.22)
    static let cian = Color(red: 0.15, green: 0.90, blue: 1.0)
    static let magenta = Color(red: 1.0, green: 0.25, blue: 0.65)
    static let lima = Color(red: 0.72, green: 1.0, blue: 0.25)
    static let naranja = Color(red: 1.0, green: 0.62, blue: 0.15)
    static let violeta = Color(red: 0.62, green: 0.40, blue: 1.0)
    static let degradado = LinearGradient(colors: [cian, violeta, magenta],
                                          startPoint: .leading, endPoint: .trailing)
}

struct ContentView: View {
    @ObservedObject var bascula: BasculaBLE
    @ObservedObject var store: AlimentosStore

    @State private var seleccion: Alimento?
    @State private var pesoPatron = "200"
    @State private var mostrarSelector = false
    @State private var busqueda = ""
    @State private var pedirPIN = false
    @State private var pinIngresado = ""
    @State private var ajustesDesbloqueados = false
    @State private var mensajeAlimentos = ""
    /// PIN para los ajustes (cámbialo antes de usarla en público).
    private let pinAjustes = "1234"

    /// El peso mostrado solo cambia si varía al menos esto (gramos), para ignorar el ruido.
    private let umbralCambio = 0.8
    @State private var pesoMostrado: Double = 0

    private var pesoCrudo: Double { bascula.lectura?.peso ?? 0 }
    private var peso: Double { pesoMostrado }
    /// Con peso negativo las calorías no cambian: se calculan con 0 g.
    private var pesoNutri: Double { max(0, peso) }
    private var factor: Double { pesoNutri / 100 }

    var body: some View {
        ZStack {
            LinearGradient(colors: [Tema.fondoArriba, Tema.fondoAbajo],
                           startPoint: .top, endPoint: .bottom)
                .ignoresSafeArea()
            Circle().fill(Tema.magenta.opacity(0.18)).frame(width: 320).blur(radius: 90)
                .offset(x: 140, y: -260).allowsHitTesting(false)
            Circle().fill(Tema.cian.opacity(0.15)).frame(width: 300).blur(radius: 90)
                .offset(x: -150, y: 300).allowsHitTesting(false)

            ScrollView {
                VStack(spacing: 16) {
                    encabezado
                    pesoCard
                    alimentoCard
                    if let a = seleccion { nutricionCard(a) }
                    if ajustesDesbloqueados { ajustesCard }
                    Text("Valores nutrimentales aproximados")
                        .font(.footnote)
                        .foregroundStyle(.white.opacity(0.45))
                        .padding(.bottom, 8)
                }
                .padding()
                .frame(maxWidth: 700)
                .frame(maxWidth: .infinity)
            }
        }
        .preferredColorScheme(.dark)
        .tint(Tema.cian)
        .onChange(of: pesoCrudo) { nuevo in
            if abs(nuevo - pesoMostrado) >= umbralCambio { pesoMostrado = nuevo }
        }
        .alert("Ajustes protegidos", isPresented: $pedirPIN) {
            SecureField("PIN", text: $pinIngresado)
            Button("Entrar") {
                if pinIngresado == pinAjustes { ajustesDesbloqueados = true }
            }
            Button("Cancelar", role: .cancel) {}
        }
        .sheet(isPresented: $mostrarSelector) { selectorAlimentos }
#if os(iOS)
        .onAppear { UIApplication.shared.isIdleTimerDisabled = true }
        .onDisappear { UIApplication.shared.isIdleTimerDisabled = false }
#endif
    }

    // MARK: - Componentes

    private func tarjeta<Contenido: View>(@ViewBuilder _ contenido: () -> Contenido) -> some View {
        VStack(alignment: .leading, spacing: 14) {
            contenido()
        }
        .padding(18)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(
            RoundedRectangle(cornerRadius: 26, style: .continuous)
                .fill(.white.opacity(0.07))
                .overlay(
                    RoundedRectangle(cornerRadius: 26, style: .continuous)
                        .strokeBorder(.white.opacity(0.14), lineWidth: 1))
        )
    }

    private var encabezado: some View {
        HStack {
            Text("NutriGo")
                .font(.system(size: 34, weight: .heavy, design: .rounded))
                .foregroundStyle(Tema.degradado)
            Spacer()
            conexionPill
            Button {
                if ajustesDesbloqueados { ajustesDesbloqueados = false }
                else { pinIngresado = ""; pedirPIN = true }
            } label: {
                Image(systemName: ajustesDesbloqueados ? "gearshape.fill" : "lock.fill")
                    .font(.headline)
                    .frame(width: 38, height: 38)
                    .background(Circle().fill(.white.opacity(0.1)))
            }
            .buttonStyle(.plain)
        }
        .padding(.top, 4)
    }

    private var conexionPill: some View {
        let ok = bascula.conectada
        return HStack(spacing: 6) {
            Circle()
                .fill(ok ? Tema.lima : Tema.naranja)
                .frame(width: 9, height: 9)
                .shadow(color: ok ? Tema.lima : Tema.naranja, radius: 4)
            Text(ok ? "Conectada" : "Buscando")
                .font(.caption.weight(.bold))
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(Capsule().fill(.white.opacity(0.1)))
    }

    private var pesoCard: some View {
        let estable = bascula.lectura?.estable == true
        return tarjeta {
            HStack {
                Label(estable ? "Estable" : "Midiendo",
                      systemImage: estable ? "checkmark.circle.fill" : "waveform")
                    .font(.caption.weight(.bold))
                    .foregroundStyle(estable ? Tema.lima : Tema.naranja)
                Spacer()
                bateriaIndicador
            }

            HStack(alignment: .firstTextBaseline, spacing: 6) {
                Text(String(format: "%.1f", peso))
                    .font(.system(size: 88, weight: .heavy, design: .rounded))
                    .minimumScaleFactor(0.5)
                    .lineLimit(1)
                    .foregroundStyle(peso < 0 ? AnyShapeStyle(Tema.magenta) : AnyShapeStyle(Tema.degradado))
                    .contentTransition(.numericText())
                Text("g")
                    .font(.title.weight(.bold))
                    .foregroundStyle(.white.opacity(0.6))
            }
            .frame(maxWidth: .infinity)

            if peso < 0 {
                Text("Peso negativo: las calorías no cambian. Pulsa Tara con la báscula vacía.")
                    .font(.caption)
                    .foregroundStyle(Tema.magenta)
                    .frame(maxWidth: .infinity)
                    .multilineTextAlignment(.center)
            } else {
                Text(bascula.estado)
                    .font(.caption)
                    .foregroundStyle(.white.opacity(0.5))
                    .frame(maxWidth: .infinity)
            }

            Button {
                bascula.tara()
                pesoMostrado = 0
            } label: {
                Label("Poner en cero", systemImage: "scalemass.fill")
                    .font(.title3.weight(.bold))
                    .foregroundStyle(.black)
                    .frame(maxWidth: .infinity, minHeight: 50)
                    .background(Capsule().fill(Tema.lima))
            }
            .buttonStyle(.plain)
        }
    }

    private var bateriaIndicador: some View {
        let mv = Double(bascula.lectura?.bateriaMV ?? 0)
        let pct = min(100, max(0, (mv - 3000) / 1200 * 100))
        let bajo = bascula.lectura?.bateriaBaja == true
        return HStack(spacing: 4) {
            Image(systemName: bateriaSimbolo(pct))
                .foregroundStyle(bajo ? Color.red : Tema.lima)
            Text(mv > 0 ? "\(Int(pct))%" : "--")
                .font(.caption.weight(.bold))
                .foregroundStyle(.white.opacity(0.7))
        }
    }

    private func bateriaSimbolo(_ pct: Double) -> String {
        switch pct {
        case ..<13: return "battery.0percent"
        case ..<38: return "battery.25percent"
        case ..<63: return "battery.50percent"
        case ..<88: return "battery.75percent"
        default:    return "battery.100percent"
        }
    }

    private var alimentoCard: some View {
        tarjeta {
            Text("¿Qué vas a pesar?")
                .font(.subheadline.weight(.bold))
                .foregroundStyle(.white.opacity(0.6))

            Button {
                mostrarSelector = true
            } label: {
                HStack {
                    Image(systemName: "fork.knife")
                        .foregroundStyle(Tema.naranja)
                    Text(seleccion?.nombre ?? "Elegir alimento")
                        .font(.title3.weight(.semibold))
                        .lineLimit(1)
                    Spacer()
                    Image(systemName: "chevron.right")
                        .font(.caption.weight(.bold))
                        .foregroundStyle(.white.opacity(0.5))
                }
                .padding(16)
                .background(RoundedRectangle(cornerRadius: 16, style: .continuous).fill(.white.opacity(0.09)))
            }
            .buttonStyle(.plain)

            if seleccion != nil && pesoNutri <= 0 {
                Text("Coloca el alimento en la báscula para ver sus valores.")
                    .font(.caption)
                    .foregroundStyle(.white.opacity(0.5))
            }
        }
    }

    private func nutricionCard(_ a: Alimento) -> some View {
        let kcal = a.kcal * factor
        let prot = a.proteina * factor
        let carb = a.carbos * factor
        let gras = a.grasa * factor
        let total = max(prot + carb + gras, 0.0001)
        return tarjeta {
            VStack(alignment: .leading, spacing: 2) {
                Text("Porción de \(String(format: "%.0f", pesoNutri)) g")
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.white.opacity(0.55))
                HStack(alignment: .firstTextBaseline, spacing: 6) {
                    Text(String(format: "%.0f", kcal))
                        .font(.system(size: 56, weight: .heavy, design: .rounded))
                        .foregroundStyle(Tema.naranja)
                        .contentTransition(.numericText())
                    Text("kcal")
                        .font(.title3.weight(.bold))
                        .foregroundStyle(.white.opacity(0.6))
                }
            }

            filaMacro("Proteína", valor: prot, fraccion: prot / total, color: Tema.cian)
            filaMacro("Carbohidratos", valor: carb, fraccion: carb / total, color: Tema.naranja)
            filaMacro("Grasa", valor: gras, fraccion: gras / total, color: Tema.magenta)
        }
        .animation(.easeInOut(duration: 0.3), value: pesoNutri)
    }

    private func filaMacro(_ titulo: String, valor: Double, fraccion: Double, color: Color) -> some View {
        VStack(spacing: 6) {
            HStack {
                Text(titulo).font(.subheadline.weight(.semibold))
                Spacer()
                Text(String(format: "%.1f g", valor))
                    .font(.subheadline.weight(.bold))
                    .foregroundStyle(color)
            }
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    Capsule().fill(.white.opacity(0.1))
                    Capsule().fill(color)
                        .frame(width: max(6, geo.size.width * min(1, fraccion)))
                        .shadow(color: color.opacity(0.7), radius: 5)
                }
            }
            .frame(height: 8)
        }
    }

    // MARK: - Ajustes

    private var ajustesCard: some View {
        tarjeta {
            Label("Configuración", systemImage: "gearshape.fill")
                .font(.headline)

            Text("Calibración")
                .font(.subheadline.weight(.bold))
                .foregroundStyle(.white.opacity(0.6))
            HStack {
                TextField("Peso patrón (g)", text: $pesoPatron)
                    .textFieldStyle(.roundedBorder)
                Button("Calibrar") {
                    if let g = Double(pesoPatron), g > 0 { bascula.calibrar(gramos: g) }
                }
                .buttonStyle(.borderedProminent)
            }
            Button(role: .destructive) {
                bascula.dormir()
            } label: {
                Label("Poner en bajo consumo", systemImage: "moon.zzz")
            }
            if !bascula.ultimaRespuesta.isEmpty {
                Text(bascula.ultimaRespuesta)
                    .font(.caption)
                    .foregroundStyle(.white.opacity(0.5))
            }

            Divider().overlay(.white.opacity(0.2))

            Text("Agregar alimento con IA")
                .font(.subheadline.weight(.bold))
                .foregroundStyle(.white.opacity(0.6))
            Text("1) Copia el formato  2) Pégalo en tu IA y escribe el alimento  3) Copia la respuesta y pulsa Pegar alimento.")
                .font(.caption)
                .foregroundStyle(.white.opacity(0.5))
            HStack(spacing: 10) {
                Button {
                    Portapapeles.copiar(AlimentosStore.formatoParaIA)
                    mensajeAlimentos = "Formato copiado. Pégalo en tu IA."
                } label: {
                    Label("Copiar formato", systemImage: "doc.on.doc")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)

                Button {
                    guard let texto = Portapapeles.leer(), !texto.isEmpty else {
                        mensajeAlimentos = "El portapapeles está vacío."
                        return
                    }
                    switch store.agregar(desdeTexto: texto) {
                    case .success(let n): mensajeAlimentos = n > 1 ? "Agregados \(n) alimentos ✓" : "Alimento agregado ✓"
                    case .failure(let e): mensajeAlimentos = e.mensaje
                    }
                } label: {
                    Label("Pegar alimento", systemImage: "doc.on.clipboard")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
            }
            if !mensajeAlimentos.isEmpty {
                Text(mensajeAlimentos)
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(Tema.lima)
            }

            if !store.propios.isEmpty {
                Text("Mis alimentos")
                    .font(.subheadline.weight(.bold))
                    .foregroundStyle(.white.opacity(0.6))
                ForEach(store.propios) { p in
                    HStack {
                        VStack(alignment: .leading) {
                            Text(p.nombre).font(.subheadline.weight(.semibold))
                            Text("\(Int(p.kcal)) kcal · P \(fmt(p.proteina)) · C \(fmt(p.carbos)) · G \(fmt(p.grasa)) /100 g")
                                .font(.caption2)
                                .foregroundStyle(.white.opacity(0.5))
                        }
                        Spacer()
                        Button(role: .destructive) {
                            if seleccion?.nombre == p.nombre { seleccion = nil }
                            store.quitar(p)
                        } label: {
                            Image(systemName: "trash")
                        }
                        .buttonStyle(.plain)
                        .foregroundStyle(Tema.magenta)
                    }
                }
            }
        }
    }

    private func fmt(_ v: Double) -> String { String(format: "%.1f", v) }

    // MARK: - Selector de alimentos

    private var selectorAlimentos: some View {
        NavigationStack {
            List {
                let filtrados = store.todos.filter {
                    busqueda.isEmpty
                        || $0.nombre.localizedCaseInsensitiveContains(busqueda)
                        || $0.categoria.localizedCaseInsensitiveContains(busqueda)
                }
                let categorias = ordenar(Array(Set(filtrados.map(\.categoria))))
                ForEach(categorias, id: \.self) { cat in
                    Section(cat) {
                        ForEach(filtrados.filter { $0.categoria == cat }) { a in
                            Button {
                                seleccion = a
                                mostrarSelector = false
                            } label: {
                                HStack {
                                    Text(a.nombre).font(.title3)
                                    Spacer()
                                    Text("\(Int(a.kcal)) kcal/100")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                }
                                .padding(.vertical, 6)
                            }
                            .buttonStyle(.plain)
                        }
                    }
                }
            }
            .searchable(text: $busqueda, prompt: "Buscar: Bimbo, Coca-Cola, gordita…")
            .navigationTitle("Elegir alimento")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cerrar") { mostrarSelector = false }
                }
            }
        }
        .preferredColorScheme(.dark)
    }

    /// "Mis alimentos" y las gorditas primero; el resto en orden alfabético.
    private func ordenar(_ cats: [String]) -> [String] {
        let prioridad = ["Mis alimentos", "Gorditas de nata"]
        return cats.sorted {
            let i = prioridad.firstIndex(of: $0) ?? prioridad.count
            let j = prioridad.firstIndex(of: $1) ?? prioridad.count
            return i == j ? $0 < $1 : i < j
        }
    }
}
