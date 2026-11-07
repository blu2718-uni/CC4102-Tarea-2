#set document(
  title: "Tarea 2: Árboles B y Bε en memoria externa",
  author: "CC4102 - Diseño y Análisis de Algoritmos",
)
#set page(
  paper: "us-letter",
  margin: (top: 2.2cm, bottom: 2.2cm, left: 2.2cm, right: 2.2cm),
  numbering: "1",
)
#set text(font: "Liberation Serif", size: 10.5pt, lang: "es")
#set par(justify: true, leading: 0.65em)
#set heading(numbering: "1.1")
#show heading.where(level: 1): set block(above: 1.3em, below: 0.75em)
#show heading.where(level: 2): set block(above: 1.05em, below: 0.6em)
#show figure.caption: set text(size: 9pt)
#show figure: set block(breakable: false)
#set figure(gap: 0.6em)
#set math.equation(numbering: "(1)")
#show raw: set text(font: "DejaVu Sans Mono", size: 0.88em)
#set list(spacing: 0.65em)
#set enum(spacing: 0.65em)
// "I/O" e "I/Os" no se cortan al final de una línea.
#show regex("I/Os?"): it => box(it)

// ---------------------------------------------------------------------------
// Valores del texto, calculados a partir de las tablas de figuras/
// (las genera graficos/graficar.py con `make graficos`)
// ---------------------------------------------------------------------------

/// Filas de un CSV de figuras/, sin el encabezado.
#let filas(nombre) = csv("figuras/" + nombre + ".csv").slice(1)

/// Número a partir de una celda ya formateada ("1 091,7" -> 1091.7).
#let num(s) = float(s.replace("\u{a0}", "").replace(" ", "").replace(",", "."))

/// Texto de un número con coma decimal, d decimales y separador de miles.
#let fmt(x, d: 0) = {
  let escala = calc.pow(10, d)
  let k = int(calc.round(calc.abs(x) * escala))
  let ent = str(calc.quo(k, escala))
  let grupos = ()
  while ent.len() > 3 {
    grupos.insert(0, ent.slice(ent.len() - 3))
    ent = ent.slice(0, ent.len() - 3)
  }
  grupos.insert(0, ent)
  let s = grupos.join("\u{a0}")
  if d > 0 { s += "," + str(calc.rem(k, escala) + escala).slice(1) }
  if x < 0 and k != 0 { "−" + s } else { s }
}

/// Duración legible a partir de segundos.
#let duracion(s) = if s >= 3600 { fmt(s / 3600, d: 1) + " horas" } else if s >= 60 {
  fmt(s / 60) + " minutos"
} else { fmt(s) + " segundos" }

// Parámetros fijos del experimento (deben coincidir con src/main.cpp).
#let busquedas-principal = 50
#let busquedas-alpha = 20
#let exp-alpha = 24
#let alpha-usado = 0.9
#let p-rango = 604800 / (1754006400 - 1546300800)

#let arboles = ("B", "BE4", "BE126")
#let raiz = (B: 1, BE4: 2, BE126: 2)
#let capacidad = (B: 340, BE4: 506, BE126: 384)

#let P = filas("tabla_principal")
#let E = filas("tabla_estructura")
#let M = filas("tabla_modelo_busqueda")
#let C = filas("tabla_consulta")
#let A = filas("tabla_alpha_BE126")
#let exps = P.map(r => int(r.at(0).split("^").at(1))).dedup()
#let nmin = exps.first()
#let nmax = exps.last()

/// Columna de tabla_principal para N = 2^e y el árbol t, como número.
#let pp(e, t, c) = {
  let col = (t_ins: 2, ios_M: 3, io_par: 4, nodos: 5, mib: 6, altura: 7, t_bus: 8, io_bus: 9, res: 10)
  num(P.find(r => r.at(0) == "2^" + str(e) and r.at(1) == t).at(col.at(c)))
}
/// Columna de tabla_estructura (N = 2^nmax) para el árbol t: celda tal cual o como número.
#let ecelda(t, c) = {
  let col = (internos: 1, hojas: 2, pct_int: 3, por_hoja: 4, vaciados: 5, lote: 6, bajadas: 7, lect: 8, esc: 9, ms_io: 10)
  E.find(r => r.at(0) == t).at(col.at(c))
}
#let ee(t, c) = num(ecelda(t, c))
/// Columna de tabla_alpha_BE126 en la fila i: celda tal cual o como número.
#let acelda(i, c) = {
  let col = (alpha: 0, t_ins: 1, io_par: 4, vac: 5, lote: 6, pct_int: 7, nodos: 8, t_bus: 9, io_bus: 11)
  A.at(i).at(col.at(c))
}
#let aa(i, c) = num(acelda(i, c))
/// Hijos promedio por nodo interno con N = 2^nmax.
#let fanout(t) = (ee(t, "internos") + ee(t, "hojas") - 1) / ee(t, "internos")

#let V = {
  let v = (:)
  let b = (:)
  let l = (:)
  let N = calc.pow(2, nmax)
  let todos-e = range(exps.len())
  /// Exponentes en que se cumple el orden Bε c=4 < Bε c=126 < B para la columna c.
  let orden(c) = exps.filter(e => pp(e, "BE4", c) < pp(e, "BE126", c) and pp(e, "BE126", c) < pp(e, "B", c))
  /// Primer exponente desde el cual f(e) se cumple para todos los siguientes (none si no hay).
  let desde(f) = {
    let i = exps.len()
    while i > 0 and f(exps.at(i - 1)) { i -= 1 }
    if i < exps.len() { exps.at(i) } else { none }
  }
  let sinraiz(e, t) = pp(e, t, "io_par") - raiz.at(t)

  v.insert("n_min", str(nmin))
  v.insert("n_max", str(nmax))
  v.insert("alpha_n", str(exp-alpha))
  v.insert("busquedas_principal", str(busquedas-principal))
  v.insert("construcciones_principal", str(arboles.len() * exps.len()))
  v.insert("busquedas_total_principal", fmt(busquedas-principal * arboles.len() * exps.len()))
  v.insert("alpha_busquedas", str(busquedas-alpha))
  v.insert("alpha_usado", fmt(alpha-usado, d: 1))

  // Creación.
  let ot = orden("t_ins")
  b.insert("orden_tiempo_todos", ot.len() == exps.len())
  v.insert("orden_tiempo_cumple", str(ot.len()) + " de los " + str(exps.len()))
  l.insert("orden_tiempo_excepciones", exps.filter(e => e not in ot))
  let oi = orden("io_par")
  b.insert("orden_io_todos", oi.len() == exps.len())
  v.insert("orden_io_cumple", str(oi.len()) + " de los " + str(exps.len()))
  for t in arboles {
    v.insert("t_ins_" + t + "_max", fmt(pp(nmax, t, "t_ins"), d: 1))
    v.insert("us_ins_" + t + "_min", fmt(pp(nmin, t, "t_ins") / calc.pow(2, nmin) * 1e6, d: 2))
    v.insert("us_ins_" + t + "_max", fmt(pp(nmax, t, "t_ins") / N * 1e6, d: 2))
    v.insert("io_par_" + t + "_min", fmt(pp(nmin, t, "io_par"), d: 3))
    v.insert("io_par_" + t + "_max", fmt(pp(nmax, t, "io_par"), d: 3))
    v.insert("sinraiz_" + t + "_min", fmt(sinraiz(nmin, t), d: 3))
    v.insert("sinraiz_" + t + "_max", fmt(sinraiz(nmax, t), d: 3))
  }
  for t in ("BE4", "BE126") {
    let sr = sinraiz(nmax, t)
    v.insert("ventaja_sinraiz_" + t, fmt(sinraiz(nmax, "B") / sr, d: 1))
    v.insert("modelo_lotes_dif_" + t, fmt(calc.abs(sr - 2 * ee(t, "bajadas") / ee(t, "lote")) / sr * 100, d: 1))
  }

  // Tamaño y altura.
  let ot = orden("nodos")
  b.insert("orden_tamano_todos", ot.len() == exps.len())
  v.insert("orden_tamano_cumple", str(ot.len()) + " de los " + str(exps.len()))
  for t in arboles {
    v.insert("nodos_" + t + "_max", fmt(pp(nmax, t, "nodos")))
    v.insert("mib_" + t + "_max", fmt(pp(nmax, t, "mib"), d: 1))
    v.insert("menos_nodos_" + t, fmt((1 - pp(nmax, t, "nodos") / pp(nmax, "B", "nodos")) * 100))
    v.insert("altura_" + t + "_min", str(int(pp(nmin, t, "altura"))))
    v.insert("altura_" + t + "_max", str(int(pp(nmax, t, "altura"))))
    l.insert("altura_" + t + "_sube", todos-e.slice(1)
      .filter(i => pp(exps.at(i), t, "altura") > pp(exps.at(i - 1), t, "altura"))
      .map(i => exps.at(i)))
    v.insert("pares_hoja_" + t, fmt(ee(t, "por_hoja")))
    v.insert("ocupacion_" + t, fmt(ee(t, "por_hoja") / capacidad.at(t) * 100))
    v.insert("pct_internos_" + t, fmt(ee(t, "pct_int"), d: 1))
  }

  // Búsquedas.
  v.insert("frac_resultados", fmt(exps.map(e => pp(e, "B", "res") / calc.pow(2, e)).sum() / exps.len() * 100, d: 3))
  v.insert("frac_teorica", fmt(p-rango * 100, d: 3))
  v.insert("res_min", fmt(pp(nmin, "B", "res")))
  v.insert("res_max", fmt(pp(nmax, "B", "res")))
  b.insert("mismos_resultados", exps.all(e => arboles.all(t => pp(e, t, "res") == pp(e, "B", "res"))))
  for t in arboles {
    v.insert("io_bus_" + t + "_min", fmt(pp(nmin, t, "io_bus"), d: 1))
    v.insert("io_bus_" + t + "_max", fmt(pp(nmax, t, "io_bus"), d: 1))
    v.insert("t_bus_" + t + "_max", fmt(pp(nmax, t, "t_bus"), d: 1))
  }
  v.insert("rel_BE4_min", fmt(pp(nmin, "BE4", "io_bus") / pp(nmin, "B", "io_bus"), d: 2))
  v.insert("rel_BE4_max", fmt(pp(nmax, "BE4", "io_bus") / pp(nmax, "B", "io_bus"), d: 2))
  v.insert("rel_BE126_max", fmt(pp(nmax, "BE126", "io_bus") / pp(nmax, "B", "io_bus"), d: 2))
  let cruce = desde(e => pp(e, "BE4", "io_bus") < calc.min(pp(e, "B", "io_bus"), pp(e, "BE126", "io_bus")))
  b.insert("hay_cruce", cruce != none)
  l.insert("cruce", if cruce == none { () } else { (cruce,) })
  let ms-io = arboles.map(t => ee(t, "ms_io"))
  v.insert("ms_io_max_min", fmt(calc.min(..ms-io), d: 3))
  v.insert("ms_io_max_max", fmt(calc.max(..ms-io), d: 3))
  let tb(e, t) = pp(e, t, "t_bus")
  l.insert("be4_lento", exps.filter(e => tb(e, "BE4") > calc.max(tb(e, "B"), tb(e, "BE126"))))
  let rapido = desde(e => tb(e, "BE4") < calc.min(tb(e, "B"), tb(e, "BE126")))
  b.insert("be4_rapido_al_final", rapido != none)
  l.insert("be4_rapido_desde", if rapido == none { () } else { (rapido,) })
  v.insert("b_vs_126_dif", fmt(calc.abs(tb(nmax, "B") - tb(nmax, "BE126")), d: 1))
  b.insert("b_mas_lento_126", tb(nmax, "B") > tb(nmax, "BE126"))

  // Búsqueda 0. El rango y el ciclo diario dependen solo de la semilla y de datos.bin,
  // no de la máquina, así que se dejan fijos.
  v.insert("c0_l", $1 thin 597 thin 289 thin 407$)
  v.insert("c0_u", $1 thin 597 thin 894 thin 207$)
  v.insert("c0_inicio", [12-08-2020 a las 23:30])
  v.insert("c0_fin", [19-08-2020 a las 23:30])
  v.insert("c0_dias", "6")
  v.insert("c0_max_diario_min", "11,7")
  v.insert("c0_max_diario_max", "18,3")
  v.insert("c0_min_diario_min", "−0,8")
  v.insert("c0_min_diario_max", "2,2")
  l.insert("consulta_fig", C.map(r => int(r.at(0).split("^").at(1))))
  v.insert("c0_mayor", str(l.consulta_fig.last()))
  for r in C {
    let e = r.at(0).split("^").at(1)
    let s = num(r.at(6))
    v.insert("c0_pares_" + e, r.at(1))
    v.insert("c0_brecha_" + e, if s >= 60 { fmt(s / 60) + " minutos" } else { fmt(s) + " segundos" })
    v.insert("c0_prom_" + e, fmt(num(r.at(5)), d: 1))
  }
  b.insert("c0_contenido", C.all(r => r.at(7) == "sí"))

  // Bonus: α con el Bε c = 126.
  let ia = range(A.len())
  let ultimo = A.len() - 1
  let io = ia.map(i => aa(i, "io_par"))
  let iob = ia.map(i => aa(i, "io_bus"))
  let tins = ia.map(i => aa(i, "t_ins"))
  let nod = ia.map(i => aa(i, "nodos"))
  let arg-min(xs) = ia.filter(i => xs.at(i) == calc.min(..xs)).first()
  v.insert("alpha_cantidad", str(A.len()))
  v.insert("alpha_lista", A.map(r => r.at(0)).join("; "))
  v.insert("a_io_primero", acelda(0, "io_par"))
  v.insert("a_io_ultimo", acelda(ultimo, "io_par"))
  v.insert("a_io_min", acelda(arg-min(io), "io_par"))
  v.insert("a_alpha_io_min", acelda(arg-min(io), "alpha"))
  v.insert("a_baja_pct", fmt((io.first() - io.last()) / io.first() * 100, d: 1))
  v.insert("a_baja_sinraiz_pct", fmt((io.first() - io.last()) / (io.first() - 2) * 100))
  let subidas = ia.slice(1).map(i => (io.at(i) - io.at(i - 1)) / io.at(i - 1) * 100)
  b.insert("a_monotona", subidas.all(s => s <= 0))
  v.insert("a_subida_max", fmt(calc.max(0, ..subidas), d: 2))
  for (k, c) in (("lote", "lote"), ("vac", "vac"), ("int", "pct_int")) {
    v.insert("a_" + k + "_primero", acelda(0, c))
    v.insert("a_" + k + "_ultimo", acelda(ultimo, c))
  }
  v.insert("a_t_min", fmt(calc.min(..tins), d: 2))
  v.insert("a_t_max", fmt(calc.max(..tins), d: 2))
  v.insert("a_alpha_t_min", acelda(arg-min(tins), "alpha"))
  let iob-var = (calc.max(..iob) - calc.min(..iob)) / calc.min(..iob) * 100
  v.insert("a_iob_min", fmt(calc.min(..iob), d: 1))
  v.insert("a_iob_max", fmt(calc.max(..iob), d: 1))
  v.insert("a_iob_var", fmt(iob-var, d: 1))
  let tbus = ia.map(i => aa(i, "t_bus"))
  v.insert("a_tb_min", fmt(calc.min(..tbus), d: 1))
  v.insert("a_tb_max", fmt(calc.max(..tbus), d: 1))
  v.insert("a_nodos_var", fmt((calc.max(..nod) - calc.min(..nod)) / calc.min(..nod) * 100, d: 1))
  let ms-io-a = ia.map(i => tbus.at(i) / iob.at(i))
  v.insert("a_ms_io_min", fmt(calc.min(..ms-io-a), d: 2))
  v.insert("a_ms_io_max", fmt(calc.max(..ms-io-a), d: 2))
  b.insert("alpha_sin_compromiso", iob-var <= 5)
  let f126 = capacidad.BE126 / fanout("BE126")
  v.insert("f_BE126", fmt(f126, d: 1))
  v.insert("alpha_min", acelda(0, "alpha"))
  v.insert("umbral_min", str(calc.ceil(aa(0, "alpha") * f126)))
  v.insert("umbral_elegido", str(calc.ceil(alpha-usado * f126)))
  let ie = ia.find(i => calc.abs(aa(i, "alpha") - alpha-usado) < 1e-9)
  b.insert("hay_elegido", ie != none)
  if ie != none {
    v.insert("a_io_elegido", acelda(ie, "io_par"))
    v.insert("a_dif_carga_elegido", fmt((io.at(ie) - calc.min(..io)) / calc.min(..io) * 100, d: 2))
    v.insert("a_dif_bus_elegido", fmt((iob.at(ie) - calc.min(..iob)) / calc.min(..iob) * 100, d: 1))
    v.insert("a_t_elegido", acelda(ie, "t_ins"))
    b.insert("a_t_elegido_es_min", tins.at(ie) == calc.min(..tins))
    v.insert("a_t_dif_pct", fmt((tins.at(ie) - calc.min(..tins)) / calc.min(..tins) * 100, d: 1))
    b.insert("a_hay_mayores", ie < ultimo)
    v.insert("a_vac_elegido", acelda(ie, "vac"))
    v.insert("a_alpha_mayor", acelda(ultimo, "alpha"))
  }

  // Análisis de la inserción.
  v.insert("esc_par_B", ecelda("B", "esc"))
  for t in ("BE4", "BE126") {
    let d = ee(t, "bajadas")
    let m = ee(t, "lote")
    let modelo = 2 + 2 * d / m
    v.insert("nodos_par_" + t, fmt(pp(nmax, t, "nodos") / N, d: 4))
    v.insert("eq_d_" + t, ecelda(t, "bajadas"))
    v.insert("eq_m_" + t, ecelda(t, "lote"))
    v.insert("eq_modelo_" + t, fmt(modelo, d: 3))
    v.insert("eq_medido_" + t, fmt(pp(nmax, t, "io_par"), d: 3))
    v.insert("eq_dif_" + t, fmt(calc.abs(pp(nmax, t, "io_par") - modelo), d: 3))
    v.insert("costo_bajada_" + t, fmt(2 / m, d: 2))
    v.insert("raiz_pct_" + t, fmt(2 / pp(nmax, t, "io_par") * 100))
    v.insert("ventaja_total_" + t, fmt(pp(nmax, "B", "io_par") / pp(nmax, t, "io_par"), d: 1))
  }
  v.insert("fanout_B", fmt(fanout("B")))
  v.insert("fanout_BE126", fmt(fanout("BE126")))
  v.insert("fanout_BE4", fmt(fanout("BE4"), d: 2))
  v.insert("fanout_teorico_BE4", fmt(capacidad.BE4 / fanout("BE4")))
  // Extrapolación: cada I/O de inserción cuesta lo mismo que una I/O de búsqueda con N = 2^nmax.
  let lat = ms-io.sum() / ms-io.len()
  v.insert("lat_busqueda", fmt(lat, d: 2))
  v.insert("ext_B", duracion(pp(nmax, "B", "ios_M") * 1e6 * lat / 1000))
  for t in arboles { v.insert("ext_" + t + "_sinraiz", duracion(sinraiz(nmax, t) * N * lat / 1000)) }
  // Tiempo por I/O simulada (µs) = t_ins / I/Os.
  let us-io(e, t) = pp(e, t, "t_ins") / pp(e, t, "ios_M")
  let chicos = exps.filter(e => e <= 20)
  let us-chico = chicos.map(e => arboles.map(t => us-io(e, t))).flatten()
  v.insert("us_io_chico_min", fmt(calc.min(..us-chico), d: 2))
  v.insert("us_io_chico_max", fmt(calc.max(..us-chico), d: 2))
  v.insert("mib_chico", fmt(calc.max(..chicos.map(e => arboles.map(t => pp(e, t, "mib"))).flatten())))
  let us-max = arboles.map(t => us-io(nmax, t))
  v.insert("us_io_max_min", fmt(calc.min(..us-max), d: 2))
  v.insert("us_io_max_max", fmt(calc.max(..us-max), d: 2))
  let gib = arboles.map(t => pp(nmax, t, "mib") / 1024)
  v.insert("mib_grande_min", fmt(calc.min(..gib), d: 2))
  v.insert("mib_grande_max", fmt(calc.max(..gib), d: 2))
  b.insert("be4_mas_barato_por_io", us-io(nmax, "BE4") == calc.min(..us-max))
  v.insert("us_io_BE4_max", fmt(us-io(nmax, "BE4"), d: 2))
  v.insert("us_io_B_max", fmt(us-io(nmax, "B"), d: 2))
  v.insert("razon_t_B_BE4", fmt(pp(nmax, "B", "t_ins") / pp(nmax, "BE4", "t_ins"), d: 1))
  v.insert("razon_io_B_BE4", fmt(pp(nmax, "B", "io_par") / pp(nmax, "BE4", "io_par"), d: 1))

  // Altura y búsqueda.
  for t in arboles {
    v.insert("logF_" + t, fmt(calc.ln(ee(t, "hojas")) / calc.ln(fanout(t)), d: 1))
    v.insert("niveles_internos_" + t, str(int(pp(nmax, t, "altura")) - 1))
  }
  let niv = pp(nmax, "BE4", "altura") - pp(nmin, "BE4", "altura")
  v.insert("be4_niveles_sumados", str(int(niv)))
  v.insert("be4_factor_por_nivel", if niv > 0 { fmt(calc.pow(2, (nmax - nmin) / niv), d: 1) } else { "-" })
  v.insert("p_pct", fmt(p-rango * 100, d: 2))
  v.insert("modelo_bus_dif_max", fmt(calc.max(..M.map(r => calc.abs(num(r.at(6))))), d: 1))
  let pn-min = arboles.map(t => p-rango * pp(nmin, t, "nodos"))
  b.insert("pn_min_menor_1", calc.max(..pn-min) < 1)
  v.insert("pn_min_max", fmt(calc.max(..pn-min), d: 2))
  let pn-max = arboles.map(t => p-rango * pp(nmax, t, "nodos"))
  v.insert("pn_max_min", fmt(calc.min(..pn-max)))
  v.insert("pn_max_max", fmt(calc.max(..pn-max)))
  let ms-io-p = exps.map(e => arboles.map(t => pp(e, t, "t_bus") / pp(e, t, "io_bus"))).flatten()
  v.insert("ms_io_min", fmt(calc.min(..ms-io-p), d: 2))
  v.insert("ms_io_max", fmt(calc.max(..ms-io-p), d: 2))

  // Conclusión.
  b.insert("h1_io", b.orden_io_todos)
  b.insert("h1", b.orden_io_todos and b.orden_tiempo_todos)
  let alt(t) = pp(nmax, t, "altura")
  b.insert("h2_alturas", alt("BE4") > alt("BE126") and alt("BE126") <= alt("B") + 1)
  b.insert("be4_mas_io_min", pp(nmin, "BE4", "io_bus") > calc.max(pp(nmin, "B", "io_bus"), pp(nmin, "BE126", "io_bus")))
  b.insert("h3", b.orden_tamano_todos)
  (v: v, b: b, l: l)
}
/// Texto ya formateado de un resultado (coma decimal, separador de miles).
#let val(k) = V.v.at(k)
/// Condición que depende de los datos (true/false).
#let cond(k) = V.b.at(k)
/// Lista de exponentes de N.
#let lst(k) = V.l.at(k)
/// $2^e$ a partir de un exponente (número o texto).
#let pot(e) = $2^#str(e)$
/// Lista de potencias en español: "2^17 y 2^25".
#let pots(xs) = {
  let p = xs.map(pot)
  if p.len() == 0 { [] } else if p.len() == 1 { p.at(0) } else { p.slice(0, -1).join([, ]) + [ y ] + p.last() }
}
#let Nmin = pot(val("n_min"))
#let Nmax = pot(val("n_max"))
#let Na = pot(val("alpha_n"))

#let Be = $B^epsilon$
#let Be4 = $B^epsilon_(c=4)$
#let Be126 = $B^epsilon_(c=126)$
#let nombres = (B: [B], BE4: [#Be4], BE126: [#Be126])
#let potencia(s) = { let e = s.split("^").at(1); $2^#e$ }

/// Tabla con filetes horizontales (estilo libro).
/// - encabezado: arreglo de celdas de encabezado (pueden tener saltos de línea con \).
/// - celdas: arreglo plano de celdas (puede incluir table.hline()).
/// - izquierda: cantidad de columnas iniciales alineadas a la izquierda; el resto va a la derecha.
/// - tam: tamaño de letra de la tabla.
#let tabla(encabezado, celdas, izquierda: 1, tam: 8.6pt) = {
  set text(size: tam, hyphenate: false)
  table(
    columns: encabezado.len(),
    align: (x, y) => (if x < izquierda { left } else { right }) + bottom,
    stroke: none,
    inset: (x: 4.2pt, y: 2.6pt),
    table.hline(stroke: 0.8pt),
    table.header(..encabezado),
    table.hline(stroke: 0.4pt),
    ..celdas,
    table.hline(stroke: 0.8pt),
  )
}

// ---------------------------------------------------------------------------
// Título
// ---------------------------------------------------------------------------

#align(center)[
  #text(size: 11pt, weight: "bold")[Universidad de Chile]\
  #text(size: 10pt)[Facultad de Ciencias Físicas y Matemáticas - Departamento de Ciencias de la Computación]\
  #text(size: 10pt)[CC4102: Diseño y Análisis de Algoritmos]\
  #v(0.8em)
  #text(size: 16pt, weight: "bold")[Tarea 2: Árboles B y #Be en memoria externa]\
  #v(0.4em)
  #text(size: 11pt)[Implementación y evaluación experimental de árboles B y Bε]\
  #v(0.8em)
  #text(size: 11pt)[Integrantes: Bastián Caballero, Julio Yáñez e Isidora Moncada]\
  #v(0.8em)
  #line(length: 100%, stroke: 0.5pt)
]

#v(0.5em)

= Introducción

En memoria externa, el costo de una operación se mide en transferencias de bloques entre el disco y la memoria principal (I/Os). El *árbol B* es la estructura clásica para un diccionario ordenado: cada nodo ocupa un bloque con $Theta(B)$ llaves, por lo que insertar y buscar cuesta $Theta(log_B N)$ I/Os. El *árbol #Be* disminuye particularmente el costo de las inserciones: cada nodo interno usa solo del orden de $B^epsilon$ llaves divisorias y destina el resto del bloque a un buffer de inserciones pendientes que guarda los elementos que van llegando; cuando el buffer se llena, las inserciones pendientes se envían por lotes hacia los hijos. Así, cada I/O de un vaciado mueve muchos pares a la vez y una inserción cuesta $O(log_B N \/ (epsilon B^(1-epsilon)))$ I/Os amortizadas, a cambio de una altura $O(log_B N \/ epsilon)$, que es lo que pagan las búsquedas.

En esta tarea se implementan en C++ un árbol B ($b = 340$) y dos árboles #Be, todos con nodos de 4096 bytes: uno con $epsilon approx$ 0,25 ($c = 4$, $l_"max"$ = 506), denotado #Be4, y otro con $epsilon approx$ 0,75 ($c = 126$, $l_"max"$ = 384), denotado #Be126. Con ellos se indexaron los primeros $N in {#Nmin, ..., #Nmax}$ pares medidos en la estación Quinta Normal entre 2019 y 2025. Los árboles se construyen sobre un arreglo de bloques en RAM que simula el disco y cuenta cada lectura y escritura; luego se guardan en un archivo binario y se hacen #val("busquedas_principal") búsquedas de rangos de una semana leyendo los nodos desde ese archivo. Se reportan el tiempo y las I/Os de la construcción, el tamaño y la altura de cada árbol, y el tiempo y las I/Os de las búsquedas. Además se estudia el parámetro $alpha$ del vaciado con $N = #Na$.

En este informe también se describen las estructuras, los algoritmos y las decisiones de implementación. Se presentan el entorno, la metodología y los resultados, que luego se examinan mediante el análisis amortizado de los vaciados y la altura en función de $epsilon$, para finalmente contrastarlos con la hipótesis inicial:

*Hipótesis:* Un bloque de 4096 bytes tiene espacio para $B = 512$ pares de 8 bytes, $B^"0,25" approx "4,8"$ y $B^"0,75" approx 108$, que corresponden aproximadamente a los 5 y 127 hijos de las dos configuraciones.

+ _Inserción._ En un árbol B, la inserción implica recorrer el camino desde la raíz hasta una hoja y modificar esa hoja: aproximadamente $(h + 1)$ I/Os por par, donde $h$ está entre 2 y 4. En un árbol #Be, cada par se deja primero en el buffer de la raíz (2 I/Os), y solo cuando el buffer se llena se envía un lote de pares a un hijo; así, el costo de ese vaciado se reparte entre todos los pares del lote. Con $epsilon approx$ 0,25, cada vaciado lleva al menos $ceil(506 \/ 5) = 102$ pares al hijo más cargado; con $epsilon approx$ 0,75, lleva solo $ceil(384 \/ 127) = 4$. Por tanto, se espera que #Be4 requiera menos I/Os por inserción que #Be126, y que ambos requieran menos que el árbol B.
+ _Altura y búsqueda._ La altura de un #Be crece como $1\/epsilon$: #Be4 tendría unas 3 veces la altura de #Be126, que a su vez debería tener casi la altura del árbol B. En búsquedas que tocan pocos nodos eso ordena los árboles como B $approx$ #Be126 < #Be4. Pero un rango de una semana contiene $approx$ 0,29 % de los datos, así que para $N$ grande debería dominar el número de bloques con resultados y los tres árboles deberían acercarse.
+ _Tamaño._ Las hojas de los #Be tienen espacio para más pares que las del árbol B (506 y 384 contra 340), por lo que ocuparían menos bloques, sobre todo en #Be4.

= Desarrollo <sec:desarrollo>

== Disco y archivo

Los nodos del disco simulado se reservan en segmentos de 1024 bloques (4 MiB) con `mmap`, incorporándolos a medida que el árbol crece. Así se evita reservar de inmediato la memoria del árbol más grande.

Las búsquedas se realizan con `O_DIRECT`, tras sincronizar el archivo y descartar sus páginas de la caché. Esto permite medir accesos al disco y no lecturas que el sistema operativo ya mantenga en RAM.

== Árbol #Be <sec:be>

- Durante un vaciado, el nodo que lo inicia se conserva en memoria y se escribe una sola vez al finalizar. Esto evita escrituras intermedias al procesar sus lotes.

- Al dividir una hoja que recibe un lote, primero se mezclan sus pares con el lote y luego se parte el resultado por la mediana; de esta forma, ambas hojas quedan dentro de su capacidad.

- Las llaves creadas por divisiones durante un vaciado se incorporan mientras el nodo padre está en memoria; antes de escribirlo, se divide por la mediana las veces necesarias. Con ello se mantiene la estructura válida sin escribir el padre después de cada división hija.

- El valor de $alpha$ se representa como un entero escalado por $10^6$ y el umbral se calcula con aritmética entera.

- Las búsquedas tratan los intervalos de los hijos como cerrados al decidir cuáles visitar. Así se incluyen todos los pares cuando una llave repetida queda a ambos lados de una llave divisoria. El resultado se ordena antes de escribir el CSV para entregar las consultas en orden de llave.

== Verificación

Cada búsqueda se compara con fuerza bruta sobre el mismo prefijo de datos y el programa termina con error ante una diferencia. Por otra parte, las pruebas automatizadas revisan invariantes de los árboles, conteos de I/O y casos con llaves repetidas y valores extremos, lo que permite detectar errores de estructura antes de analizar las mediciones.

= Resultados <sec:resultados>

== Entorno <sec:entorno>

#figure(
  tabla(
    ([*Componente*], [*Valor*]),
    (
      [Sistema operativo], [Ubuntu / Linux kernel 7.0.0-38-generic (x86_64)],
      [Procesador], [AMD Ryzen 7 7840HS (8 núcleos, 16 hilos)],
      [Cachés], [L1d: 32 KiB × 8 (256 KiB); L1i: 32 KiB × 8 (256 KiB); L2: 1 MiB × 8 (8 MiB); L3: 16 MiB],
      [RAM], [14 GiB DDR5 (swap de 511 MiB)],
      [Compilador], [`g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic`],
      [Informe], [Typst #sys.version],
    ),
    izquierda: 2,
  ),
  caption: [Entorno de hardware y software en que se ejecutaron los experimentos.],
) <tab:entorno>

== Datos y metodología

`datos.bin` (512 MiB, con el sha256 publicado) contiene $2^26 = 67 thin 108 thin 864$ pares en orden aleatorio, con llaves en tiempo Unix entre $1 thin 546 thin 300 thin 802$ (01-01-2019 00:00:02 UTC) y $1 thin 754 thin 006 thin 394$ (31-07-2025 23:59:54 UTC), 11 llaves repetidas y temperaturas entre −2,53 y 38,32 °C. Para cada $N$ se usan los primeros $N$ pares; como el archivo está desordenado, cada prefijo es una muestra aleatoria de todo el período y está contenido en los prefijos más grandes.

*Experimento principal.* Cada combinación ($N$, árbol) se construyó una vez (#val("construcciones_principal") construcciones) y cada construcción se siguió de las mismas #val("busquedas_principal") búsquedas (#val("busquedas_total_principal") en total, todas iguales a la fuerza bruta). El tiempo y las I/Os de búsqueda son el promedio de las #val("busquedas_principal") búsquedas. Ambos árboles #Be usan $alpha = #val("alpha_usado")$ (justificado en la @sec:alpha).

*Bonus.* Con $N = #Na$, para el #Be126 ($epsilon approx$ 0,75) y cada uno de los #val("alpha_cantidad") valores de $alpha$ (#val("alpha_lista")) se construyó el árbol y se hicieron las mismas #val("alpha_busquedas") búsquedas (generadas con otra semilla).

Los tiempos dependen de la máquina y de su carga al medir; las I/Os, el tamaño y la altura no.

== Creación

#figure(
  image("figuras/creacion_tiempo.svg", width: 100%),
  caption: [Tiempo de creación (inserción de los $N$ pares) en función de $N$: (a) total; (b) por par insertado.],
) <fig:tiempo>

#figure(
  image("figuras/creacion_ios.svg", width: 100%),
  caption: [I/Os simuladas al insertar los $N$ pares: (a) totales; (b) por par insertado; (c) por par sin contar la raíz (1 lectura en el árbol B, 1 lectura y 1 escritura en los #Be). La línea punteada es $2 dot "lotes" \/ N$: leer y escribir el hijo en cada lote de un vaciado.],
) <fig:ios>

- El tiempo de creación crece casi linealmente con $N$ (@fig:tiempo a). El orden #Be4 < #Be126 < B se cumple #if cond("orden_tiempo_todos") [para todo $N$] else [en #val("orden_tiempo_cumple") valores de $N$ (no en #pots(lst("orden_tiempo_excepciones")))]. Con $N = #Nmax$: #val("t_ins_BE4_max") s, #val("t_ins_BE126_max") s y #val("t_ins_B_max") s.
- Por par insertado (@fig:tiempo b), entre $N = #Nmin$ y $N = #Nmax$:
  - El árbol B pasa de #val("us_ins_B_min") a #val("us_ins_B_max") µs.
  - El #Be126 pasa de #val("us_ins_BE126_min") a #val("us_ins_BE126_max") µs.
  - El #Be4 pasa de #val("us_ins_BE4_min") a #val("us_ins_BE4_max") µs.
- Las I/Os por inserción (@fig:ios b):
  - Pasan de #val("io_par_B_min") a #val("io_par_B_max") en el árbol B.
  - Pasan de #val("io_par_BE126_min") a #val("io_par_BE126_max") en el #Be126.
  - Pasan de #val("io_par_BE4_min") a #val("io_par_BE4_max") en el #Be4.
  El orden #Be4 < #Be126 < B se cumple #if cond("orden_io_todos") [para todo $N$] else [en #val("orden_io_cumple") valores de $N$].
- Sin contar la raíz (@fig:ios c), con $N = #Nmax$:
  - El árbol B hace #val("sinraiz_B_max") I/Os por inserción.
  - El #Be126 hace #val("sinraiz_BE126_max") I/Os por inserción (#val("ventaja_sinraiz_BE126") veces menos que el árbol B).
  - El #Be4 hace #val("sinraiz_BE4_max") I/Os por inserción (#val("ventaja_sinraiz_BE4") veces menos que el árbol B).

#let estructura = filas("tabla_estructura")
#let medidas = (
  [Nodos internos], [Hojas], [% de los pares en nodos internos], [Pares por hoja (promedio)], [Vaciados],
  [Pares por lote (promedio)], [Bajadas por par], [Lecturas por par insertado], [Escrituras por par insertado],
  [ms por I/O de búsqueda],
)
#figure(
  tabla(
    ([*Medida*],) + estructura.map(r => [*#nombres.at(r.at(0))*]),
    range(medidas.len()).map(i => (medidas.at(i),) + estructura.map(r => r.at(i + 1))).flatten(),
    tam: 8.8pt,
  ),
  caption: [Estructura de los árboles y contadores de la inserción con $N = #Nmax$. _Bajadas por par_: total de pares bajados un nivel (sumando todos los lotes) dividido por $N$. La última fila es el tiempo de búsqueda dividido por sus I/Os.],
) <tab:estructura>

== Tamaño y altura

#figure(
  image("figuras/tamano_altura.svg", width: 100%),
  caption: [(a) Tamaño de los árboles (cantidad de nodos por 4096 bytes, que es el tamaño del archivo) y (b) altura, en función de $N$.],
) <fig:tamano>

- El tamaño crece proporcionalmente a $N$ (@fig:tamano a). Con $N = #Nmax$:
  - El árbol B usa #val("nodos_B_max") nodos (#val("mib_B_max") MiB).
  - El #Be126 usa #val("nodos_BE126_max") nodos (#val("mib_BE126_max") MiB, #val("menos_nodos_BE126") % menos).
  - El #Be4 usa #val("nodos_BE4_max") nodos (#val("mib_BE4_max") MiB, #val("menos_nodos_BE4") % menos).
  El orden #if cond("orden_tamano_todos") [es el mismo para todo $N$] else [se cumple en #val("orden_tamano_cumple") valores de $N$].
- En cuanto a la altura (@fig:tamano b):
  - El árbol B pasa de #val("altura_B_min") a #val("altura_B_max") (sube en #pots(lst("altura_B_sube"))).
  - El #Be126 pasa de #val("altura_BE126_min") a #val("altura_BE126_max") (sube en #pots(lst("altura_BE126_sube"))).
  - El #Be4 pasa de #val("altura_BE4_min") a #val("altura_BE4_max") (sube en #pots(lst("altura_BE4_sube"))).
- En promedio, cada hoja almacena:
  - En el árbol B, #val("pares_hoja_B") pares (#val("ocupacion_B") % de 340).
  - En el #Be126, #val("pares_hoja_BE126") pares (#val("ocupacion_BE126") % de 384).
  - En el #Be4, #val("pares_hoja_BE4") pares (#val("ocupacion_BE4") % de 506).
  Además, el #val("pct_internos_BE4") % de los pares del #Be4 está en buffers de nodos internos, contra #val("pct_internos_BE126") % en el #Be126 y #val("pct_internos_B") % en los nodos internos del árbol B (@tab:estructura).

== Búsquedas

#figure(
  image("figuras/busqueda.svg", width: 100%),
  caption: [Búsquedas de rangos de una semana (promedio de las #val("busquedas_principal") búsquedas) en función de $N$: (a) tiempo por búsqueda y (b) I/Os por búsqueda, con barras de ± una desviación estándar; (c) I/Os de cada árbol divididas por las del árbol B.],
) <fig:busqueda>

- Cada búsqueda devolvió en promedio el #val("frac_resultados") % de los $N$ pares (lo esperado para rangos de una semana es #val("frac_teorica") %), desde #val("res_min") pares con $N = #Nmin$ hasta #val("res_max") con $N = #Nmax$#if cond("mismos_resultados") [; los tres árboles devolvieron los mismos pares].
- Las I/Os por búsqueda (@fig:busqueda b) crecen casi proporcionalmente a $N$ cuando $N$ es grande:
  - Con $N = #Nmin$, el #Be4 lee #val("io_bus_BE4_min") bloques, contra #val("io_bus_B_min") del árbol B (#val("rel_BE4_min") veces) y #val("io_bus_BE126_min") del #Be126.
  - #if cond("hay_cruce") [Desde $N = #pots(lst("cruce"))$, el #Be4 es el que menos lee (@fig:busqueda c)] else [El #Be4 no llega a ser el que menos lee]: con $N = #Nmax$ hace #val("io_bus_BE4_max") I/Os, contra #val("io_bus_BE126_max") del #Be126 y #val("io_bus_B_max") del árbol B (#val("rel_BE4_max") y #val("rel_BE126_max") veces las del árbol B).
- El tiempo de búsqueda (@fig:busqueda a) sigue a las I/Os. Con $N = #Nmax$ alcanza:
  - #val("t_bus_BE4_max") ms en el #Be4.
  - #val("t_bus_BE126_max") ms en el #Be126.
  - #val("t_bus_B_max") ms en el árbol B.
  Esto equivale a entre #val("ms_io_max_min") y #val("ms_io_max_max") ms por I/O (@tab:estructura). El #Be4 es el más lento de los tres #if lst("be4_lento").len() > 0 [con $N$ = #pots(lst("be4_lento"))] else [en ningún $N$]#if cond("be4_rapido_al_final") [ y el más rápido desde $N = #pots(lst("be4_rapido_desde"))$]. Entre el árbol B y el #Be126, con $N = #Nmax$ la diferencia es de #val("b_vs_126_dif") ms (#if cond("b_mas_lento_126") [el árbol B es el más lento] else [el #Be126 es el más lento]). Como cada búsqueda se midió una sola vez, las diferencias de tiempo chicas pueden deberse a la variación del disco.

== Una misma búsqueda con dos valores de N

#figure(
  image("figuras/consulta_misma_busqueda.svg", width: 100%),
  caption: [Pares que devuelve la búsqueda 0, $[#val("c0_l"), #val("c0_u")]$ (del #val("c0_inicio") al #val("c0_fin"), hora de Chile), con $N = #pots(lst("consulta_fig"))$.],
) <fig:consulta>

#figure(
  tabla(
    ([*N*], [*Pares*], [*% de N*], [*Mín.\ (°C)*], [*Máx.\ (°C)*], [*Prom.\ (°C)*], [*Separación \ mediana (s)*], [*¿Contenido \ en el de $2^#val("c0_mayor")$?*]),
    filas("tabla_consulta").map(r => (potencia(r.at(0)),) + r.slice(1)).flatten(),
  ),
  caption: [Resumen de la búsqueda 0 para varios valores de $N$. _Separación mediana_: mediana del tiempo entre dos mediciones consecutivas del resultado.],
) <tab:consulta>

#let ca = str(lst("consulta_fig").first())
#let cb = str(lst("consulta_fig").last())
La búsqueda 0 cubre una semana (@fig:consulta). Con $N = 2^#ca$ devuelve #val("c0_pares_" + ca) mediciones, separadas por #val("c0_brecha_" + ca) en la mediana, y con $N = 2^#cb$ devuelve #val("c0_pares_" + cb), separadas por #val("c0_brecha_" + cb) (@tab:consulta). Las dos muestran el mismo ciclo diario: en los #val("c0_dias") días completos del rango, el máximo diario va de #val("c0_max_diario_min") a #val("c0_max_diario_max") °C y el mínimo diario de #val("c0_min_diario_min") a #val("c0_min_diario_max") °C (con $N = 2^#cb$). El promedio de la semana cambia poco entre ambos valores de $N$ (#val("c0_prom_" + ca) y #val("c0_prom_" + cb) °C). #if cond("c0_contenido") [Todos los pares de los resultados con $N$ menor aparecen en el de $N = 2^#val("c0_mayor")$, porque los prefijos están contenidos unos en otros.] Con $N = 2^#ca$ se pierden las variaciones que duran menos que la separación entre mediciones, que con $N = 2^#cb$ se ven como una curva casi continua.

== Bonus: elección de α <sec:alpha>

#figure(
  image("figuras/alpha_BE126.svg", width: 94%),
  caption: [Experimento de $alpha$ con el #Be126 y $N = #Na$. (a) Tiempo de creación; (b) I/Os por inserción; (c) tiempo por búsqueda: promedio de #val("alpha_busquedas") búsquedas, barras de ± un error estándar; (d) I/Os por búsqueda. Las escalas de (b) y (d) van de 5 % bajo el mínimo a 5 % sobre el máximo.],
) <fig:alpha126>

#let encabezado-alpha = ([*α*], [*$t$ creación \ (s)*], [*desv. \ (s)*], [*I/Os \ por par*], [*Vaciados*], [*Pares \ por lote*], [*% pares \ en internos*], [*Nodos*], [*$t$ búsq. \ (ms)*], [*err. \ est. (ms)*], [*I/Os \ búsq.*])
// Columnas de tabla_alpha_*.csv que se muestran (se omite la 3, I/Os totales en millones).
#let columnas-alpha(r) = (0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11).map(i => r.at(i))

#figure(
  tabla(encabezado-alpha, filas("tabla_alpha_BE126").map(columnas-alpha).flatten()),
  caption: [Experimento de $alpha$ con el #Be126 y $N = #Na$. _desv._ es la desviación estándar del tiempo de creación (0 con una sola construcción); el tiempo de búsqueda es el promedio de las #val("alpha_busquedas") búsquedas y _err. est._ su error estándar.],
) <tab:alpha126>

En el #Be126 (@fig:alpha126, @tab:alpha126), las I/Os de carga pasan de #val("a_io_primero") por inserción con el menor $alpha$ a #val("a_io_ultimo") con el mayor (mínimo: #val("a_io_min"), con $alpha = #val("a_alpha_io_min")$), una baja de #val("a_baja_pct") % (#val("a_baja_sinraiz_pct") % sin contar la raíz). El tiempo de creación queda entre #val("a_t_min") y #val("a_t_max") s (el menor con $alpha = #val("a_alpha_t_min")$). Las I/Os de búsqueda quedan entre #val("a_iob_min") y #val("a_iob_max") (#val("a_iob_var") % de diferencia) y el tiempo de búsqueda entre #val("a_tb_min") y #val("a_tb_max") ms.

#if cond("hay_elegido") [
*Elección.* Se escogió $alpha = #val("alpha_usado")$, que es el que usan ambos #Be en el experimento principal. Sus I/Os de carga (#val("a_io_elegido") por par) están #val("a_dif_carga_elegido") % sobre el mínimo y sus I/Os de búsqueda, #val("a_dif_bus_elegido") % sobre el mínimo. #if cond("a_t_elegido_es_min") [Además tuvo el menor tiempo de creación (#val("a_t_elegido") s).] else [Su tiempo de creación (#val("a_t_elegido") s) fue #val("a_t_dif_pct") % mayor que el menor; con una sola construcción por $alpha$, esa diferencia puede deberse a la variación de la máquina.]#if cond("a_hay_mayores") [ Un $alpha$ más cercano a 1 haría más vaciados (#val("a_vac_ultimo") con $alpha = #val("a_alpha_mayor")$ contra #val("a_vac_elegido")), es decir, más trabajo de CPU, sin una ganancia apreciable en I/Os.] #if cond("alpha_sin_compromiso") [Las I/Os de búsqueda cambian a lo más #val("a_iob_var") % entre todos los $alpha$, así que en este experimento una mejor carga no empeoró la búsqueda.] else [Las I/Os de búsqueda cambian #val("a_iob_var") % entre los $alpha$ probados: hay un compromiso entre carga y búsqueda.] La @sec:analisis-alpha explica estos resultados.
]

= Análisis <sec:analisis>

== Costo amortizado de la inserción

*Árbol B.* Cada inserción lee los $h$ nodos del camino y escribe la hoja. Un split escribe 3 bloques y ocurre en promedio una vez cada $approx #val("pares_hoja_B")$ inserciones en una hoja (cada split crea una hoja nueva, y las hojas tienen en promedio #val("pares_hoja_B") pares), lo que suma unas $3 \/ #val("pares_hoja_B")$ escrituras por inserción: con $N = #Nmax$ el árbol B hace #val("esc_par_B") escrituras por par (exactamente $1 + N + 3 dot "splits"$ escrituras en total). Por lo tanto, las I/Os por inserción son $h + 1$ más esa pequeña fracción, lo que explica los escalones de la @fig:ios b: suben un poco más de 1 cada vez que el árbol gana un nivel (en #pots(lst("altura_B_sube"))). Cuando la altura sube durante la construcción, las primeras inserciones se hicieron con un nivel menos, así que el promedio queda entre dos escalones.

*Árbol #Be.* Cada inserción cuesta 2 I/Os en la raíz. El resto son lotes: cada uno cuesta 2 I/Os (leer y escribir el hijo), más una escritura por nodo nuevo. Las lecturas son exactamente $N + "lotes"$ y las escrituras $N + "lotes" + "nodos"$, y los nodos son pocos (#val("nodos_par_BE4") y #val("nodos_par_BE126") por inserción con $N = #Nmax$). Si $overline(m)$ es el tamaño promedio de un lote y $overline(d)$ el número promedio de niveles que baja cada par,
$ "I/Os por inserción" approx 2 + (2 overline(d)) / overline(m). $ <eq:costo>
Con los contadores de la @tab:estructura, la @eq:costo da $2 + 2 dot #val("eq_d_BE4") \/ #val("eq_m_BE4") = #val("eq_modelo_BE4")$ para #Be4 y $2 + 2 dot #val("eq_d_BE126") \/ #val("eq_m_BE126") = #val("eq_modelo_BE126")$ para #Be126, a #val("eq_dif_BE4") y #val("eq_dif_BE126") de lo medido (#val("eq_medido_BE4") y #val("eq_medido_BE126")). Esa diferencia son las escrituras de los nodos nuevos, que la ecuación no incluye. La @fig:ios c muestra que ese término explica las I/Os sin la raíz para todo $N$.

El análisis amortizado acota ambos factores. Un par baja a lo más $h - 1 = O(log_(B^epsilon) N) = O(log_B N \/ epsilon)$ niveles. Y cada lote mueve muchos pares si el buffer está casi lleno al vaciarse, lo que se cumple siempre en la raíz (se vacía con $l_"max"$ pares) y en los demás nodos cuando se vacían porque no les cabe un lote: con $approx l_"max"$ pares repartidos entre a lo más $c + 1 approx B^epsilon$ hijos, el hijo más poblado recibe al menos $f approx l_"max" \/ (c + 1) = Omega(B^(1-epsilon))$ pares, y cada hijo elegido, al menos $alpha f$. Con un argumento como el de la función potencial, en el que cada par en un buffer guarda _"crédito"_ para sus bajadas futuras y cada bajada cuesta $O(1 \/ (alpha B^(1-epsilon)))$ I/Os, el costo amortizado de los vaciados es
$ O(h / (alpha B^(1-epsilon))) = O((log_B N) / (alpha epsilon B^(1-epsilon))) "I/Os por inserción". $
En los datos, con $epsilon approx$ 0,25 los lotes son grandes ($overline(m) = #val("eq_m_BE4")$, del orden de $506 \/ #val("fanout_BE4") approx #val("fanout_teorico_BE4")$, porque los nodos internos tienen en promedio #val("fanout_BE4") hijos): aunque cada par baja #val("eq_d_BE4") niveles, cada bajada cuesta solo #val("costo_bajada_BE4") I/Os, básicamente gratis. Con $epsilon approx$ 0,75 los lotes son de #val("eq_m_BE126") pares: cada par baja solo #val("eq_d_BE126") niveles, pero cada bajada cuesta #val("costo_bajada_BE126") I/Os. La teoría predice que el costo del árbol B dividido por el de un #Be (sin la raíz) es $Theta(epsilon B^(1-epsilon))$: $approx 27$ para $epsilon = "0,25"$ y $approx "3,6"$ para $epsilon = "0,75"$. Lo medido con $N = #Nmax$ es #val("ventaja_sinraiz_BE4") y #val("ventaja_sinraiz_BE126"). Como $overline(d)$ crece con la altura, el costo sin la raíz de los #Be crece lentamente con $N$ (de #val("sinraiz_BE4_min") a #val("sinraiz_BE4_max") en #Be4, mientras la altura pasa de #val("altura_BE4_min") a #val("altura_BE4_max")).

*La raíz domina.* En el modelo de la tarea la raíz se lee y se escribe en cada inserción, y esas 2 I/Os son el #val("raiz_pct_BE4") % de las I/Os del #Be4 y el #val("raiz_pct_BE126") % de las del #Be126. Por eso, contando todo, la ventaja de los #Be sobre el árbol B es solo de #val("ventaja_total_BE4") y #val("ventaja_total_BE126") veces. En un sistema real la raíz (y probablemente los primeros niveles) estaría siempre en RAM, y la diferencia que importa es la de la @fig:ios c. Como ilustración, si cada I/O costara lo que costó en promedio una I/O de búsqueda con $N = #Nmax$ (#val("lat_busqueda") ms por bloque, @tab:estructura), insertar los $#Nmax$ pares directamente en disco tomaría unas #val("ext_B") con el árbol B (#val("ext_B_sinraiz") con la raíz en RAM), mientras que con la raíz en RAM tomaría unas #val("ext_BE126_sinraiz") con el #Be126 y #val("ext_BE4_sinraiz") con el #Be4. Es una extrapolación gruesa, porque ignora la caché del sistema operativo y que las escrituras se pueden agrupar, pero ilustra el punto.

*Tiempo.* En la simulación, el tiempo de creación es aproximadamente las I/Os por el costo de copiar un bloque de 4 KiB: #val("us_io_chico_min")–#val("us_io_chico_max") µs por I/O con $N <= 2^20$, cuando los árboles (≤ #val("mib_chico") MiB) caben en buena parte en las cachés, y #val("us_io_max_min")–#val("us_io_max_max") µs con $N = #Nmax$, cuando ocupan #val("mib_grande_min")–#val("mib_grande_max") GiB. #if cond("be4_mas_barato_por_io") [El #Be4 es el más barato por I/O (#val("us_io_BE4_max") µs contra #val("us_io_B_max") µs del árbol B) porque casi todas sus I/Os son la raíz, que se usa en cada inserción y por eso se mantiene en la caché. ]Por eso la ventaja en tiempo puede ser mayor que en I/Os: con $N = #Nmax$, el árbol B tarda #val("razon_t_B_BE4") veces lo que el #Be4 y hace #val("razon_io_B_BE4") veces sus I/Os.

== Altura en función de ε y costo de búsqueda

*Altura.* En un árbol con $H$ hojas cuyos nodos internos tienen en promedio $F$ hijos, $h approx 1 + log_F H$. Con $N = #Nmax$, $F$ es #val("fanout_B") en el árbol B, #val("fanout_BE126") en el #Be126 y #val("fanout_BE4") en el #Be4. Eso da $log_F H$ = #val("logF_B"), #val("logF_BE126") y #val("logF_BE4"). La fórmula es aproximada porque supone que todos los niveles tienen la misma cantidad de hijos, y cerca de la raíz no es así (la raíz puede tener pocos hijos). La razón entre los niveles internos de los dos #Be se compara con la razón 3 entre sus $1\/epsilon$, que predice $h = O(log_B N \/ epsilon)$. Con $F approx 3$, el #Be4 debería sumar un nivel cada vez que $N$ se multiplica por $approx 3$; en los datos sumó #val("be4_niveles_sumados") niveles entre $#Nmin$ y $#Nmax$, uno por cada factor de $approx #val("be4_factor_por_nivel")$. Los otros dos suman un nivel cada vez que $N$ se multiplica por un factor del orden de su $F$.

*Búsqueda.* Un rango que cubre una fracción $p$ del espacio de llaves intersecta $approx p dot n_ell + 1$ de los $n_ell$ nodos de cada nivel $ell$, así que
$ "I/Os por búsqueda" approx p dot n + h, $ <eq:busqueda>
donde $n$ es el total de nodos. Con $p = 604 thin 800 \/ 207 thin 705 thin 600 = #val("p_pct") %$, la @eq:busqueda queda a menos de #val("modelo_bus_dif_max") % de lo medido en los $N$ de la @tab:modelo. Esto explica la @fig:busqueda c:
- Con $N$ chico#if cond("pn_min_menor_1") [, $p dot n$ es menor que 1 (a lo más #val("pn_min_max") con $N = #Nmin$) y] manda la altura: con $N = #Nmin$, el #Be4 ($h = #val("altura_BE4_min")$) hace #val("rel_BE4_min") veces las I/Os del árbol B ($h = #val("altura_B_min")$).
- Con $N$ grande manda $p dot n$ (con $N = #Nmax$, de #val("pn_max_min") a #val("pn_max_max"), contra alturas de #val("altura_B_max") a #val("altura_BE4_max")), y el orden lo da el tamaño. El #Be4 tiene #val("menos_nodos_BE4") % menos nodos que el árbol B porque sus hojas guardan #val("pares_hoja_BE4") pares en promedio contra #val("pares_hoja_B"), y porque guarda el #val("pct_internos_BE4") % de los pares en buffers de nodos internos, que en el árbol B solo sirven para dirigir la búsqueda. #if cond("hay_cruce") [Las curvas se cruzan cerca de $N = #pots(lst("cruce"))$, cuando la diferencia de tamaño compensa la diferencia de altura.]

Por lo tanto, para estos rangos el árbol más bajo no es necesariamente el más rápido, sino el más compacto. En una búsqueda puntual el costo sería solo la altura (#val("altura_B_max"), #val("altura_BE126_max") y #val("altura_BE4_max") I/Os con $N = #Nmax$ para cada uno de los árboles), y el árbol B y el #Be126 serían más baratos que el #Be4. El tiempo de búsqueda es casi exactamente las I/Os por la latencia del disco: entre #val("ms_io_min") y #val("ms_io_max") ms por I/O en todos los $N$ y árboles, así que revisar los buffers de los nodos visitados no tiene un costo de CPU apreciable.

#figure(
  tabla(
    ([*N*], [*Árbol*], [*Nodos $n$*], [*Altura $h$*], [*$p dot n + h$*], [*I/Os medidas*], [*Diferencia (%)*]),
    filas("tabla_modelo_busqueda").map(r => (potencia(r.at(0)), nombres.at(r.at(1))) + r.slice(2)).flatten(),
    izquierda: 2,
  ),
  caption: [I/Os por búsqueda: modelo de la @eq:busqueda y valor medido (promedio de todas las búsquedas).],
) <tab:modelo>

== Efecto de α <sec:analisis-alpha>

Un lote cuesta 2 I/Os sin importar cuántos pares lleve, así que bajar grupos chicos es lo que desperdicia I/Os: si se dejan en el buffer, bajan más tarde junto con los pares que lleguen después. El umbral $ceil(alpha f)$ decide cuáles grupos bajan. Un $alpha$ alto hace más vaciados, porque cada uno libera menos espacio, pero en esta implementación un vaciado no cuesta I/Os propias: la raíz se lee en cada inserción y un nodo interno se vacía cuando ya se leyó para recibir un lote. Solo cuesta CPU (agrupar el buffer). Por eso las I/Os de carga tienden a bajar al subir $alpha$#if cond("a_monotona") [, y en #Be126 bajaron de forma monótona] else [, salvo variaciones de hasta #val("a_subida_max") %, porque $alpha$ también cambia la forma del árbol].

El efecto depende de cuántos pares le tocan a cada hijo. En #Be4, un vaciado reparte $approx 500$ pares entre unos #val("fanout_BE4") hijos, así que los grupos tienen más de cien pares y los lotes ya son grandes (#val("eq_m_BE4") pares en promedio con $alpha = #val("alpha_usado")$, @tab:estructura): se espera que ahí $alpha$ ahorre poco, aunque no se midió. En #Be126, en cambio, $f approx 384 \/ #val("fanout_BE126") approx #val("f_BE126")$: con tan pocos pares por hijo, es esperable que el tamaño de los grupos varíe mucho de un hijo a otro. Con $alpha = #val("alpha_min")$ el umbral es #val("umbral_min") y baja casi todo grupo no vacío; con $alpha = #val("alpha_usado")$ el umbral es #val("umbral_elegido") y los grupos chicos esperan. Ahí $alpha$ cambia el costo en un #val("a_baja_sinraiz_pct") % sin contar la raíz.

En la búsqueda, $alpha$ cambia cuántos pares quedan en buffers internos (de #val("a_int_primero") % a #val("a_int_ultimo") % en #Be126), pero esos nodos se leen igual, porque están en el camino de la búsqueda. Según la @eq:busqueda lo que importa es el número de nodos, que con $alpha$ cambia a lo más #val("a_nodos_var") %, y las I/Os de búsqueda cambian a lo más #val("a_iob_var") %. El tiempo por I/O de búsqueda varía entre #val("a_ms_io_min") y #val("a_ms_io_max") ms entre valores de $alpha$, así que las diferencias de tiempo de búsqueda entre $alpha$ vienen sobre todo de la variación de la latencia del disco. #if cond("alpha_sin_compromiso") [Así, el compromiso que menciona el enunciado (mejor carga a cambio de peor búsqueda) no aparece en este experimento: la búsqueda casi no depende de $alpha$, y el criterio para elegirlo es la carga.] else [En este experimento sí aparece un compromiso entre carga y búsqueda en al menos un árbol, que la regla de elección resuelve con su tolerancia de búsqueda.]

= Conclusión <sec:conclusion>

Se implementaron un árbol B y dos árboles #Be con nodos de 4096 bytes. Se construyeron sobre un disco simulado que cuenta cada transferencia de bloque, se guardaron en un archivo y se buscaron leyendo de él con `O_DIRECT`, para $N = #Nmin, ..., #Nmax$ pares reales de temperatura. Todas las búsquedas se verificaron contra fuerza bruta y las invariantes de las estructuras se probaron por separado.

*Hipótesis.*
+ _Inserción: #if cond("h1") [se cumplió] else if cond("h1_io") [se cumplió en I/Os y en parte en tiempo] else [no se cumplió del todo]._ En I/Os el orden #Be4 < #Be126 < B se cumplió #if cond("orden_io_todos") [para todo $N$] else [en #val("orden_io_cumple") valores de $N$], y en tiempo #if cond("orden_tiempo_todos") [también para todo $N$] else [en #val("orden_tiempo_cumple")]: con $N = #Nmax$, #val("io_par_BE4_max"), #val("io_par_BE126_max") y #val("io_par_B_max") I/Os por inserción, y #val("t_ins_BE4_max"), #val("t_ins_BE126_max") y #val("t_ins_B_max") s. Las I/Os de los #Be están dominadas por las 2 de la raíz; sin ellas, la diferencia con el árbol B es de #val("ventaja_sinraiz_BE4") y #val("ventaja_sinraiz_BE126") veces, del orden de $epsilon B^(1-epsilon)$ que predice el análisis amortizado. La causa es el tamaño de los lotes: #val("eq_m_BE4") pares por lote con $epsilon approx$ 0,25 contra #val("eq_m_BE126") con 0,75.
+ _Altura y búsqueda: #if cond("h2_alturas") and cond("hay_cruce") [se cumplió en parte] else if cond("h2_alturas") [se cumplió] else [no se cumplió]._ #if cond("h2_alturas") [Las alturas fueron las esperadas] else [Las alturas no fueron las esperadas] (#val("altura_BE4_max") contra #val("altura_BE126_max") y #val("altura_B_max") con $N = #Nmax$)#if cond("be4_mas_io_min") [, y con $N$ chico el #Be4 fue el peor en búsquedas (#val("rel_BE4_min") veces las I/Os del árbol B con $#Nmin$)]. #if cond("hay_cruce") [Pero desde $N = #pots(lst("cruce"))$ fue el que menos leyó, no solo similar a los otros: con $N = #Nmax$ hizo #val("rel_BE4_max") veces las I/Os del árbol B. No se anticipó que una búsqueda que devuelve cientos de bloques cuesta $p dot n + h$, y que los buffers del #Be también guardan datos, lo que lo hace el árbol más compacto.] else [Con $N$ grande los tres árboles se acercaron, como se esperaba (#val("rel_BE4_max") y #val("rel_BE126_max") veces las I/Os del árbol B con $N = #Nmax$).]
+ _Tamaño: #if cond("h3") [se cumplió] else [no se cumplió para todo $N$]._ Con $N = #Nmax$, el #Be4 usó #val("menos_nodos_BE4") % menos bloques que el árbol B y el #Be126, #val("menos_nodos_BE126") % menos.

En el bonus, el costo de carga del #Be126 baja al subir $alpha$#if cond("a_monotona") [ (de forma monótona)], mientras que la búsqueda #if cond("alpha_sin_compromiso") [casi no depende de $alpha$] else [sí depende de $alpha$]. Por eso se usó $alpha = #val("alpha_usado")$ en ambos #Be.

*Limitaciones y mejoras.*
- Las inserciones se midieron en RAM. Medir algunas construcciones directamente en disco permitiría validar la extrapolación de la @sec:analisis.
- Solo se usaron rangos de una semana. Variar el ancho del rango (desde búsquedas puntuales hasta meses) mostraría en qué punto conviene cada árbol, según la @eq:busqueda.
- Los tiempos dependen de la máquina (@tab:entorno) y de la carga que tenía al medir, y cada construcción se midió una sola vez; repetir las construcciones y usar la mediana daría tiempos más robustos. Las I/Os, el tamaño y la altura no dependen de la máquina.
- No se implementó el borrado (no se pedía), ni la carga masiva de datos ordenados, que permitiría construir árboles completamente llenos.

#heading(numbering: none)[Anexo: resultados completos del experimento principal]

#let celdas-principal = {
  let celdas = ()
  for (i, r) in filas("tabla_principal").enumerate() {
    if i > 0 and r.at(1) == "B" { celdas.push(table.hline(stroke: 0.3pt + luma(160))) }
    celdas += (if r.at(1) == "B" { potencia(r.at(0)) } else { [] }, nombres.at(r.at(1))) + r.slice(2)
  }
  celdas
}

#figure(
  tabla(
    ([*N*], [*Árbol*], [*$t$ creación \ (s)*], [*I/Os creación \ (millones)*], [*I/Os \ por par*], [*Nodos*], [*MiB*], [*Altura*], [*$t$ búsqueda \ (ms)*], [*I/Os \ búsqueda*], [*Resultados*]),
    celdas-principal,
    izquierda: 2,
    tam: 8.4pt,
  ),
  caption: [Resultados del experimento principal. El tiempo, las I/Os y los resultados de búsqueda son promedios de las #val("busquedas_principal") búsquedas. Las I/Os de creación son las del disco simulado y las de búsqueda son bloques leídos del archivo.],
) <tab:principal>
