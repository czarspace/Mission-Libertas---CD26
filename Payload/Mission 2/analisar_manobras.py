#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Libertas - Missão 2: para um satélite desviado (Beatriz, atrás da Gabriella, ou Carolina,
à frente), dado (altitude inicial, distância inicial), testa se é possível CRAVAR a
distância-alvo (1500 km) a 400 km de altitude por três estratégias:

  (1) 1º ramo  - impulso 1 tangencial + impulso 2 no 1º cruzamento da elipse com r = 400 km
  (2) 2º ramo  - idem, mas o impulso 2 é feito no 2º cruzamento
  (3) Faseamento - transferência até uma órbita de espera (h_fase), espera, transferência de
                   volta aos 400 km (4 impulsos; a espera é calculada em forma fechada)

Antes disso, uma regra de decisão (banda de controle) diz se é preciso corrigir: se a distância
está em 1500 ± 5% e a altitude a ±10 km de 400 km, o estado é NOMINAL e nada é feito.

Para cada estratégia imprime: viável ou não, o ΔV de cada impulso, o ΔV total, a duração e a
altitude/distância durante a manobra (verificadas por propagação numérica RK4 independente).

Convenções
  * "atrás/frente" em relação à Gabriella e ao sentido do movimento orbital (B-G-C).
  * Fase relativa com sinal:  phi < 0 = atrás (Beatriz),  phi > 0 = à frente (Carolina).
  * Ganho de fase de um trecho = (ângulo percorrido pelo satélite) - n_ref * (tempo de voo).
  * Modelo: órbitas coplanares, dois corpos, impulsos instantâneos (sem J2 nem arrasto).

Uso:
    python3 analisar_manobras.py                       # exemplos
    python3 analisar_manobras.py --sat Beatriz --h 350 --d 1425
    python3 analisar_manobras.py --mapa Carolina       # varre vários estados iniciais
"""
import math
import argparse

# ============================== PARÂMETROS ==================================
GM = 398600.4418            # [km^3/s^2]
R_TERRA = 6371.0            # [km]
H_REF = 400.0               # altitude da Gabriella [km]
D_ALVO = 1500.0             # distância-alvo [km]
TOL_DIST_KM = 1.0           # tolerância para considerar a distância "cravada" [km]

ALT_MAX = 450.0             # teto de altitude ao longo de TODA a manobra [km]
ALT_MIN_PISO = 350.0        # piso de altitude (nunca abaixo de min(piso, altitude inicial)) [km]
ENVELOPE = (1000.0, 2000.0) # distância permitida durante a manobra [km]; None desliga

BANDA_REL = 0.05            # banda de controle: nominal se |d - D_ALVO| <= 5% de D_ALVO (±75 km)
TOL_ALT_KM = 10.0           # e se |h - H_REF| <= 10 km; fora disso, dispara a correção

DV_MAX_ABS = 0.5            # |ΔV1| máximo varrido nas estratégias (1) e (2) [km/s]
DV_PASSO = 0.0005           # passo da varredura de ΔV1 [km/s] (0,5 m/s)
T_ESP_MAX_ORBITAS = 3.0     # espera máxima do faseamento, em períodos orbitais
H_FASE_PASSO = 1.0          # passo da varredura da altitude de espera [km]
N_VERIFICA = 80             # nº máximo de candidatos de faseamento verificados por RK4

R_REF = R_TERRA + H_REF
N_REF = math.sqrt(GM / R_REF**3)
T_REF = 2 * math.pi / N_REF
TWO_PI = 2 * math.pi
deg = math.degrees


# ============================== GEOMETRIA ===================================
def corda_para_angulo(R1, R2, d):
    """Lei dos Cossenos: ângulo central [rad] entre dois pontos a raios R1, R2 e distância d."""
    c = (R1**2 + R2**2 - d**2) / (2 * R1 * R2)
    return math.acos(max(-1.0, min(1.0, c)))


def angulo_para_corda(R1, R2, x):
    return math.sqrt(R1**2 + R2**2 - 2 * R1 * R2 * math.cos(x))


def normaliza(phi):
    """Leva o ângulo para (-pi, pi]."""
    return (phi + math.pi) % TWO_PI - math.pi


def vis_viva(R, a):
    return math.sqrt(GM * (2 / R - 1 / a))


ALPHA = corda_para_angulo(R_REF, R_REF, D_ALVO)      # ângulo-alvo (12,72° para 1500 km)


def estado_inicial(lado, h, d):
    s = -1 if lado == "atras" else 1
    R_q = R_TERRA + h
    x1 = corda_para_angulo(R_q, R_REF, d)
    return dict(lado=lado, s=s, h=h, d=d, R_q=R_q, x1=x1, phi0=s * x1, phi_alvo=s * ALPHA,
                ganho_nec=normaliza(s * ALPHA - s * x1),
                alt_min=min(ALT_MIN_PISO, h))


# ===================== ESTRATÉGIAS (1) e (2): TRANSFERÊNCIA DIRETA ==========
def elipse_apos_impulso(R_q, dv):
    """vis-viva: elipse após ΔV1 tangencial a partir da órbita circular de raio R_q."""
    vc = math.sqrt(GM / R_q)
    den = 2 / R_q - (vc + dv)**2 / GM
    if den <= 0:
        return None
    a = 1 / den
    e = abs(1 - R_q / a)
    if e < 1e-9:
        return None
    return a, e, a * (1 - e), a * (1 + e)               # a, e, r_perigeu, r_apogeu


def _anomalia_media(f, e):
    E = (2 * math.atan2(math.sqrt(1 - e) * math.sin(f / 2),
                        math.sqrt(1 + e) * math.cos(f / 2))) % TWO_PI
    return E - e * math.sin(E)


def cruzamentos(R_q, dv, alt_min):
    """Ramos 1 e 2: ganho de fase, tempo de voo e ΔV2 até cada cruzamento da elipse com
    r = R_REF. dv > 0 -> parte do perigeu; dv < 0 -> parte do apogeu.
    Respeita o teto de altitude (apogeu) e o piso (perigeu)."""
    el = elipse_apos_impulso(R_q, dv)
    if el is None:
        return {}
    a, e, rp, ra = el
    if ra - R_TERRA > ALT_MAX + 1e-6 or rp - R_TERRA < alt_min - 1e-6:
        return {}
    n_e = math.sqrt(GM / a**3)
    T = TWO_PI / n_e
    f0 = 0.0 if dv > 0 else math.pi
    v_t = vis_viva(R_REF, a)
    v_c = math.sqrt(GM / R_REF)
    out = {}

    def registra(ramo, varredura, tof, f_cruz):
        gamma = math.atan2(e * math.sin(f_cruz), 1 + e * math.cos(f_cruz))
        dv2 = math.sqrt(v_c**2 + v_t**2 - 2 * v_c * v_t * math.cos(gamma))
        out[ramo] = dict(ramo=ramo, dv1=dv, dv2=dv2, ganho=varredura - N_REF * tof,
                         tof=tof, gamma=gamma, a=a, e=e, rp=rp, ra=ra,
                         dv2_rad=v_t * abs(math.sin(gamma)),
                         dv2_tan=abs(v_c - v_t * math.cos(gamma)))

    if abs(R_q - R_REF) < 1e-6:                         # mesma altitude: volta completa
        registra(2, TWO_PI, T, f0)
        return out
    if not (rp <= R_REF <= ra):
        return {}
    cos_f = (a * (1 - e * e) / R_REF - 1) / e
    fs = math.acos(max(-1.0, min(1.0, cos_f)))
    fins = [fs, TWO_PI - fs] if dv > 0 else [TWO_PI - fs, TWO_PI + fs]
    for ramo, fe in enumerate(fins, 1):
        dM = (_anomalia_media(fe, e) - _anomalia_media(f0, e)) % TWO_PI
        registra(ramo, fe - f0, dM / n_e, fe % TWO_PI)
    return out


def grade_dv(R_q, alt_min):
    """Valores de ΔV1 a varrer (inclui as fronteiras exatas: Hohmann, teto e piso)."""
    vc = math.sqrt(GM / R_q)
    n = int(DV_MAX_ABS / DV_PASSO)
    pts = set()
    if R_q < R_REF - 1e-6:
        sinais = [1]
    elif R_q > R_REF + 1e-6:
        sinais = [-1]
    else:
        sinais = [1, -1]
    for sg in sinais:
        pts.update(sg * i * DV_PASSO for i in range(1, n + 1))
    eps = 1e-9
    if 1 in sinais:                                     # partida no perigeu: apogeu varia
        for ap in (H_REF, ALT_MAX):
            if R_TERRA + ap >= R_q:
                a = (R_q + R_TERRA + ap) / 2
                pts.add(vis_viva(R_q, a) - vc + (eps if ap == H_REF else -eps))
    if -1 in sinais:                                    # partida no apogeu: perigeu varia
        for pe in (H_REF, alt_min):
            if R_TERRA + pe <= R_q:
                a = (R_q + R_TERRA + pe) / 2
                pts.add(vis_viva(R_q, a) - vc + (-eps if pe == H_REF else eps))
    return sorted(pts)


def raizes_ramo(est, ramo):
    """Todos os ΔV1 tais que ganho_ramo(ΔV1) = ganho necessário (bisseção)."""
    R_q, alt_min, alvo = est["R_q"], est["alt_min"], est["ganho_nec"]
    grade = grade_dv(R_q, alt_min)
    g = lambda dv: (cruzamentos(R_q, dv, alt_min).get(ramo) or {}).get("ganho")
    raizes = []
    ant = None
    for dv in grade:
        v = g(dv)
        if v is not None and ant is not None and ant[1] is not None:
            f0, f1 = ant[1] - alvo, v - alvo
            if f0 == 0 or f0 * f1 < 0:
                lo, hi = ant[0], dv
                for _ in range(80):
                    mid = (lo + hi) / 2
                    gm = g(mid)
                    if gm is None:
                        break
                    if (g(lo) - alvo) * (gm - alvo) <= 0:
                        hi = mid
                    else:
                        lo = mid
                c = cruzamentos(R_q, (lo + hi) / 2, alt_min).get(ramo)
                if c:
                    raizes.append(c)
        ant = (dv, v)
    return raizes


def intervalo_ganho(est, ramo):
    """Intervalo de ganho de fase que o ramo consegue produzir (para explicar a falha)."""
    gs = []
    for dv in grade_dv(est["R_q"], est["alt_min"]):
        c = cruzamentos(est["R_q"], dv, est["alt_min"]).get(ramo)
        if c:
            gs.append(c["ganho"])
    return (min(gs), max(gs)) if gs else None


def plano_direto(c, R_q):
    """Impulso 1 tangencial (leva a velocidade a v_circular + ΔV1) e impulso 2 vetorial,
    que no cruzamento zera a componente radial e ajusta a velocidade à circular."""
    ev = [dict(t=0.0, tipo="tang", v_alvo=math.sqrt(GM / R_q) + c["dv1"], dv=abs(c["dv1"]),
               desc="tangencial no " + ("perigeu (prógrado)" if c["dv1"] > 0 else "apogeu (retrógrado)")),
          dict(t=c["tof"], tipo="circ", dv=c["dv2"],
               desc=f"vetorial no cruzamento (radial {c['dv2_rad']*1000:.1f} + "
                    f"tangencial {c['dv2_tan']*1000:.1f} m/s)")]
    return ev


# ===================== ESTRATÉGIA (3): FASEAMENTO ============================
def eventos_hohmann(Ra, Rb, t0):
    a = (Ra + Rb) / 2
    v_a, v_b = math.sqrt(GM / Ra), math.sqrt(GM / Rb)
    v1, v2 = vis_viva(Ra, a), vis_viva(Rb, a)
    T = math.pi * math.sqrt(a**3 / GM)
    ev = [dict(t=t0, tipo="tang", v_alvo=v1, dv=abs(v1 - v_a),
               desc=f"sai da órbita de {Ra-R_TERRA:.0f} km para transferência a {Rb-R_TERRA:.0f} km"),
          dict(t=t0 + T, tipo="circ", dv=abs(v_b - v2),
               desc=f"circulariza em {Rb-R_TERRA:.0f} km")]
    return ev, T


def plano_faseamento(R_q, R_f, t_esp):
    """Hohmann R_q->R_f, espera t_esp em R_f, Hohmann R_f->R_REF.
    'ganho_transf' = soma de (pi - n_ref*T) dos trechos de transferência."""
    ev, t, ganho = [], 0.0, 0.0
    if abs(R_q - R_f) > 1e-6:
        e1, T1 = eventos_hohmann(R_q, R_f, t)
        ev += e1
        t += T1
        ganho += math.pi - N_REF * T1
    t_ini_espera = t
    t += t_esp
    e2, T2 = eventos_hohmann(R_f, R_REF, t)
    ev += e2
    t += T2
    ganho += math.pi - N_REF * T2
    return dict(eventos=ev, t_total=t, ganho_transf=ganho, R_f=R_f, t_esp=t_esp,
                t_ini_espera=t_ini_espera, dv_total=sum(e["dv"] for e in ev))


def candidatos_faseamento(est):
    """Para cada altitude de espera h_fase, calcula em forma fechada a espera que cravar a fase."""
    R_q, alt_min = est["R_q"], est["alt_min"]
    t_max = T_ESP_MAX_ORBITAS * T_REF
    hs = []
    h = alt_min
    while h <= ALT_MAX + 1e-9:
        hs.append(h)
        h += H_FASE_PASSO
    if alt_min <= est["h"] <= ALT_MAX:
        hs.append(est["h"])                              # esperar na própria órbita
    cands = []
    for h_f in sorted(set(round(x, 6) for x in hs)):
        if abs(h_f - H_REF) < 0.5:
            continue                                     # a 400 km não há deriva
        R_f = R_TERRA + h_f
        p0 = plano_faseamento(R_q, R_f, 0.0)
        dn = math.sqrt(GM / R_f**3) - N_REF              # taxa de deriva relativa [rad/s]
        falta = est["phi_alvo"] - est["phi0"] - p0["ganho_transf"]
        t_esp = (falta / dn) % (TWO_PI / abs(dn))        # menor espera >= 0 que fecha a fase
        if t_esp <= t_max:
            p = plano_faseamento(R_q, R_f, t_esp)
            p["h_fase"] = h_f
            p["dn"] = dn
            cands.append(p)
    return cands


# ==================...