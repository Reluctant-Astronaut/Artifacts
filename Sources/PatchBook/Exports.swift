import SwiftUI
import AppKit
import UniformTypeIdentifiers

enum Exports {
    static let nb = Locale(identifier: "nb_NO")

    static func number(_ v: Double?) -> String {
        guard let v = v else { return "" }
        let f = NumberFormatter()
        f.locale = nb
        f.numberStyle = .decimal
        f.usesGroupingSeparator = false
        f.maximumFractionDigits = 2
        return f.string(from: NSNumber(value: v)) ?? ""
    }

    static func date(_ d: Date?) -> String {
        guard let d = d else { return "" }
        let f = DateFormatter()
        f.dateFormat = "dd.MM.yyyy"
        return f.string(from: d)
    }

    static func longDate(_ d: Date) -> String {
        let f = DateFormatter()
        f.locale = nb
        f.dateFormat = "d. MMMM yyyy"
        return f.string(from: d)
    }

    static func stamp() -> String {
        let f = DateFormatter()
        f.dateFormat = "yyyy-MM-dd"
        return f.string(from: Date())
    }

    // MARK: CSV (Norwegian: semicolons, decimal comma, UTF-8 with BOM so Excel reads æøå)

    static func csv(_ s: StudioState) -> String {
        let header = ["Navn", "Produsent", "Modell", "Kategori", "Serienummer", "Produksjonsår", "Kjøpsdato",
                      "Kjøpspris (NOK)", "Gjenanskaffelsesverdi (NOK)", "Kjøpt hos", "Plassering", "Status",
                      "Solgt dato", "Solgt for (NOK)", "Patchbar", "Innganger", "Utganger", "Bilder", "Notater"]
        func q(_ v: String) -> String {
            "\"" + v.replacingOccurrences(of: "\"", with: "\"\"") + "\""
        }
        var lines = [header.map(q).joined(separator: ";")]
        let items = s.devices.sorted {
            ($0.category.norwegian, $0.name) < ($1.category.norwegian, $1.name)
        }
        for d in items {
            let row: [String] = [
                d.name, d.maker, d.model, d.category.norwegian, d.serial, d.yearMade, date(d.purchaseDate),
                number(d.purchasePrice), number(d.replacementValue), d.whereBought, d.location?.norwegian ?? "",
                d.status.norwegian, date(d.soldDate), number(d.soldPrice), d.patchable ? "Ja" : "Nei",
                String(d.inputs.filter { !$0.isPair }.count), String(d.outputs.filter { !$0.isPair }.count),
                String(d.photos.count), d.notes
            ]
            lines.append(row.map(q).joined(separator: ";"))
        }
        return "\u{FEFF}" + lines.joined(separator: "\r\n") + "\r\n"
    }

    /// Returns a message for the status line, or nil if cancelled.
    static func saveCSV(_ s: StudioState) -> String? {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.commaSeparatedText]
        panel.nameFieldStringValue = "Utstyrsliste-\(stamp()).csv"
        guard panel.runModal() == .OK, let url = panel.url else { return nil }
        do {
            try csv(s).data(using: .utf8)?.write(to: url, options: .atomic)
            return "CSV saved."
        } catch {
            return "Couldn't save the CSV."
        }
    }

    // MARK: Insurance PDF (Norwegian, A4)

    static let pageSize = CGSize(width: 595, height: 842)
    static let perPage = 5

    @MainActor
    static func saveInsurancePDF(_ s: StudioState) -> String? {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.pdf]
        panel.nameFieldStringValue = "Forsikring-utstyrsliste-\(stamp()).pdf"
        guard panel.runModal() == .OK, let url = panel.url else { return nil }
        return makeInsurancePDF(s, url: url) ? "Insurance PDF saved." : "Couldn't create the PDF."
    }

    @MainActor
    static func makeInsurancePDF(_ s: StudioState, url: URL) -> Bool {
        let items = s.devices.filter { !$0.isSold }.sorted {
            ($0.category.norwegian, $0.name) < ($1.category.norwegian, $1.name)
        }
        var chunks: [[Device]] = []
        var i = 0
        while i < items.count {
            chunks.append(Array(items[i..<min(i + perPage, items.count)]))
            i += perPage
        }
        let totalPages = chunks.count + 1
        var box = CGRect(origin: .zero, size: pageSize)
        guard let ctx = CGContext(url as CFURL, mediaBox: &box, nil) else { return false }

        func draw<V: View>(_ view: V) {
            let renderer = ImageRenderer(content: view
                .frame(width: pageSize.width, height: pageSize.height)
                .environment(\.colorScheme, .light))
            renderer.render { _, renderIn in
                ctx.beginPDFPage(nil)
                renderIn(ctx)
                ctx.endPDFPage()
            }
        }

        draw(PDFCover(state: s, items: items))
        for (n, chunk) in chunks.enumerated() {
            draw(PDFItemsPage(items: chunk, page: n + 2, of: totalPages))
        }
        ctx.closePDF()
        return true
    }
}

// MARK: - PDF pages (drawn with SwiftUI, light colours for print)

private let ink = Color(red: 0.12, green: 0.13, blue: 0.12)
private let soft = Color(red: 0.42, green: 0.44, blue: 0.42)
private let rule = Color(red: 0.85, green: 0.86, blue: 0.84)
private let accent = Color(red: 0.25, green: 0.33, blue: 0.22)

struct PDFCover: View {
    let state: StudioState
    let items: [Device]

    var body: some View {
        let total = items.compactMap { $0.replacementValue }.reduce(0, +)
        let paid = items.compactMap { $0.purchasePrice }.reduce(0, +)
        let missing = items.filter { $0.replacementValue == nil }.count
        VStack(alignment: .leading, spacing: 0) {
            Text("ONE BRIGHT PEARL STUDIO")
                .font(.system(size: 11, weight: .semibold))
                .tracking(2)
                .foregroundColor(accent)
            Text("Utstyrsliste for forsikring")
                .font(.system(size: 30, weight: .bold))
                .foregroundColor(ink)
                .padding(.top, 8)
            Text("Datert \(Exports.longDate(Date()))")
                .font(.system(size: 13))
                .foregroundColor(soft)
                .padding(.top, 6)
            Rectangle().fill(rule).frame(height: 1).padding(.vertical, 28)
            Text("Forsikringstaker")
                .font(.system(size: 10, weight: .semibold))
                .tracking(1)
                .foregroundColor(soft)
            Text(state.owner.name.isEmpty ? "—" : state.owner.name)
                .font(.system(size: 15, weight: .semibold))
                .foregroundColor(ink)
                .padding(.top, 4)
            Text(state.owner.address)
                .font(.system(size: 13))
                .foregroundColor(ink)
                .padding(.top, 2)
            Rectangle().fill(rule).frame(height: 1).padding(.vertical, 28)
            HStack(alignment: .top, spacing: 40) {
                summary("Antall enheter", "\(items.count)")
                summary("Samlet gjenanskaffelsesverdi", nok(total))
                summary("Samlet innkjøpspris", paid > 0 ? nok(paid) : "—")
            }
            if missing > 0 {
                Text("\(missing) enhet\(missing == 1 ? "" : "er") mangler gjenanskaffelsesverdi og er ikke med i summen.")
                    .font(.system(size: 11))
                    .foregroundColor(soft)
                    .padding(.top, 14)
            }
            Spacer()
            Text("Alle beløp i norske kroner. Gjenanskaffelsesverdi er anslått pris for å erstatte utstyret i dag. Solgt utstyr er ikke med.")
                .font(.system(size: 10))
                .foregroundColor(soft)
            Text("Side 1")
                .font(.system(size: 9))
                .foregroundColor(soft)
                .padding(.top, 8)
        }
        .padding(56)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(Color.white)
    }

    func summary(_ label: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(label.uppercased())
                .font(.system(size: 9, weight: .semibold))
                .tracking(1)
                .foregroundColor(soft)
            Text(value)
                .font(.system(size: 18, weight: .semibold))
                .foregroundColor(ink)
        }
    }
}

struct PDFItemsPage: View {
    let items: [Device]
    let page: Int
    let of: Int

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("Utstyrsliste — One Bright Pearl Studio")
                    .font(.system(size: 10, weight: .semibold))
                    .foregroundColor(accent)
                Spacer()
                Text("Side \(page) av \(of)")
                    .font(.system(size: 9))
                    .foregroundColor(soft)
            }
            .padding(.bottom, 12)
            ForEach(items) { d in
                row(d)
                Rectangle().fill(rule).frame(height: 1)
            }
            Spacer()
        }
        .padding(.horizontal, 44)
        .padding(.vertical, 40)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(Color.white)
    }

    func row(_ d: Device) -> some View {
        // Front photo plus up to two others (receipt first); serial-plate photos are left out.
        let others = d.photos.filter { $0.id != d.frontPhoto?.id && $0.kind != .serial }
            .sorted { ($0.kind == .receipt ? 0 : 1) < ($1.kind == .receipt ? 0 : 1) }
            .prefix(2)
        let purchaseParts: [String?] = [
            d.purchaseDate.map { "Kjøpt \(Exports.date($0))" },
            d.purchasePrice.map { nok($0) },
            d.whereBought.isEmpty ? nil : "hos \(d.whereBought)"
        ]
        let purchase = purchaseParts.compactMap { $0 }
        let placeParts: [String?] = [d.category.norwegian, d.location?.norwegian, d.status.norwegian]
        let place = placeParts.compactMap { $0 }
        return HStack(alignment: .top, spacing: 14) {
            photo(d.frontPhotoURL, 120, 90)
            VStack(alignment: .leading, spacing: 3) {
                Text(d.name)
                    .font(.system(size: 13, weight: .bold))
                    .foregroundColor(ink)
                if !d.makerModel.isEmpty {
                    Text(d.makerModel).font(.system(size: 11)).foregroundColor(ink)
                }
                Text(place.joined(separator: " · "))
                    .font(.system(size: 10))
                    .foregroundColor(soft)
                if !d.yearMade.isEmpty {
                    Text("Produsert: \(d.yearMade)").font(.system(size: 10)).foregroundColor(soft)
                }
                if !purchase.isEmpty {
                    Text(purchase.joined(separator: ", ")).font(.system(size: 10)).foregroundColor(soft)
                }
                HStack(spacing: 6) {
                    ForEach(Array(others)) { p in
                        photo(Store.photoURL(d.id, p.file), 52, 39)
                    }
                }
                .padding(.top, 2)
            }
            Spacer(minLength: 8)
            VStack(alignment: .trailing, spacing: 2) {
                Text("GJENANSKAFFELSE")
                    .font(.system(size: 7, weight: .semibold))
                    .tracking(0.8)
                    .foregroundColor(soft)
                Text(nok(d.replacementValue))
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundColor(ink)
            }
        }
        .padding(.vertical, 12)
    }

    @ViewBuilder
    func photo(_ url: URL?, _ w: CGFloat, _ h: CGFloat) -> some View {
        if let u = url, let img = Thumbs.image(u, max: Int(max(w, h) * 3)) {
            Image(nsImage: img)
                .resizable()
                .aspectRatio(contentMode: .fill)
                .frame(width: w, height: h)
                .clipShape(RoundedRectangle(cornerRadius: 4))
        } else {
            RoundedRectangle(cornerRadius: 4)
                .fill(Color(red: 0.94, green: 0.94, blue: 0.93))
                .frame(width: w, height: h)
                .overlay(Text("Uten bilde").font(.system(size: 8)).foregroundColor(soft))
        }
    }
}
