# Figuras do relatório em EN e PT: (1) elipses de transferência (ilustrativas),
# (2) estratégias 1-3 (movimento relativo + altitude/distância), (3) mapa de viabilidade.
import math, os, pickle, sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.colors import ListedColormap
from matplotlib.patches import Patch
import analisar_manobras as A

T = {
 "en": dict(
   strat=["Strategy 1: 1st crossing","Strategy 2: 2nd crossing","Strategy 3: phasing"],
   total="total $\\Delta V$", dur="duration", minutes="min",
   along="position along the track relative to Gabriella [km]", altrel="altitude $-$ 400 km [km]",
   relt="(a) Motion in Gabriella's frame (axes with different scales)",
   timet="(b) Altitude and distance over time",
   start="start", gab="Gabriella", target="target (1500 km)", ceil="ceiling 450 km", burns="burns",
   time="time [min]", alt="altitude [km]", dist="distance to Gabriella [km]",
   # ellipses
   earth="Earth", ref="400 km orbit (reference)", ell="transfer ellipse", p1="path to the 1st crossing",
   p2="extra path to the 2nd crossing", rest="rest of the ellipse", motion="direction of motion",
   ca="(a) Carolina (ahead), starting below 400 km:\nburn at the perigee", cb="(b) Beatriz (behind), starting above 400 km:\nburn at the apogee",
   cross1="1st crossing", cross2="2nd crossing", burn1="$\\Delta V_1$ (tangential)",
   scale_note="Eccentricity exaggerated for visibility (real transfers in this work have $e<0.012$)",
   # map
   mtitle="{}: feasible strategies", mdv="{}: minimum $\\Delta V$ [m/s]", xl="initial distance [km]", yl="initial altitude [km]",
   leg=["none","1st/2nd crossing only","phasing only","crossing and phasing"]),
 "pt": dict(
   strat=["Estratégia 1: 1º cruzamento","Estratégia 2: 2º cruzamento","Estratégia 3: faseamento"],
   total="$\\Delta V$ total", dur="duração", minutes="min",
   along="posição ao longo da trilha relativa à Gabriella [km]", altrel="altitude $-$ 400 km [km]",
   relt="(a) Movimento no referencial da Gabriella (eixos com escalas diferentes)",
   timet="(b) Altitude e distância no tempo",
   start="início", gab="Gabriella", target="alvo (1500 km)", ceil="teto 450 km", burns="impulsos",
   time="tempo [min]", alt="altitude [km]", dist="distância à Gabriella [km]",
   earth="Terra", ref="órbita de 400 km (referência)", ell="elipse de transferência", p1="trecho até o 1º cruzamento",
   p2="trecho extra até o 2º cruzamento", rest="resto da elipse", motion="sentido do movimento",
   ca="(a) Carolina (à frente), partindo abaixo de 400 km:\nimpulso no perigeu", cb="(b) Beatriz (atrás), partindo acima de 400 km:\nimpulso no apogeu",
   cross1="1º cruzamento", cross2="2º cruzamento", burn1="$\\Delta V_1$ (tangencial)",
   scale_note="Excentricidade exagerada para visualização (as transferências reais deste trabalho têm $e<0{,}012$)",
   mtitle="{}: estratégias viáveis", mdv="{}: $\\Delta V$ mínimo [m/s]", xl="distância inicial [km]", yl="altitude inicial [km]",
   leg=["nenhuma","só 1º/2º cruzamento","só faseamento","cruzamento e faseamento"]),
}
os.makedirs("figs", exist_ok=True)

def fmt(lang, x, nd=1):
    s = f"{x:.{nd}f}"
    return s.replace(".", ",") if lang == "pt" else s

# ---------------------------------------------------------------- trajetória (RK4)
def traj(est, eventos, dt=2.0, t_extra=900.0):
    R_q=est["R_q"]; th=est["phi0"]; v0=math.sqrt(A.GM/R_q)
    x,y=R_q*math.cos(th),R_q*math.sin(th); vx,vy=-v0*math.sin(th),v0*math.cos(th)
    ev=sorted(eventos,key=lambda e:e["t"]); t_fim=ev[-1]["t"]+t_extra
    t=0.0;k=0;out=[(t,x,y)];marks=[]
    acel=lambda px,py:(-A.GM*px/(px*px+py*py)**1.5,-A.GM*py/(px*px+py*py)**1.5)
    while t<t_fim-1e-9:
        while k<len(ev) and ev[k]["t"]<=t+1e-9:
            e=ev[k];k+=1;vel=math.hypot(vx,vy)
            if e["tipo"]=="tang":
                f=e["v_alvo"]/vel;vx*=f;vy*=f
            else:
                r=math.hypot(x,y);vc=math.sqrt(A.GM/r);vx,vy=-vc*y/r,vc*x/r
            marks.append(t)
        prox=ev[k]["t"] if k<len(ev) else t_fim
        h=min(dt,prox-t)
        if h<=1e-9: continue
        a1=acel(x,y);k1=(vx,vy,*a1)
        a2=acel(x+.5*h*k1[0],y+.5*h*k1[1]);k2=(vx+.5*h*k1[2],vy+.5*h*k1[3],*a2)
        a3=acel(x+.5*h*k2[0],y+.5*h*k2[1]);k3=(vx+.5*h*k2[2],vy+.5*h*k2[3],*a3)
        a4=acel(x+h*k3[0],y+h*k3[1]);k4=(vx+h*k3[2],vy+h*k3[3],*a4)
        x+=h/6*(k1[0]+2*k2[0]+2*k3[0]+k4[0]);y+=h/6*(k1[1]+2*k2[1]+2*k3[1]+k4[1])
        vx+=h/6*(k1[2]+2*k2[2]+2*k3[2]+k4[2]);vy+=h/6*(k1[3]+2*k2[3]+2*k3[3]+k4[3])
        t+=h;out.append((t,x,y))
    return np.array(out),marks

def fig_estrategia(lang,nome,h,d,idx,arq):
    L=T[lang]
    est=A.estado_inicial("atras" if nome=="Beatriz" else "frente",h,d)
    res=[A.analisa_direto(est,1),A.analisa_direto(est,2),A.analisa_faseamento(est)][idx]
    assert res["viavel"],res["motivo"]
    tr,marks=traj(est,res["eventos"]); t=tr[:,0];x=tr[:,1];y=tr[:,2]
    alt=np.hypot(x,y)-A.R_TERRA
    gx=A.R_REF*np.cos(A.N_REF*t);gy=A.R_REF*np.sin(A.N_REF*t)
    dist=np.hypot(x-gx,y-gy)
    c,s=np.cos(-A.N_REF*t),np.sin(-A.N_REF*t); rx=x*c-y*s; ry=x*s+y*c
    radial=np.hypot(rx,ry)-A.R_REF; along=A.R_REF*np.arctan2(ry,rx)
    dt0=t[1]-t[0]
    fig,ax=plt.subplots(1,2,figsize=(12.5,4.6))
    a=ax[0]
    a.plot(along,radial,color="#c0392b",lw=1.6)
    a.plot(along[0],radial[0],"o",color="#c0392b",label=L["start"])
    a.plot(0,0,"s",color="#1b4f8a",label=L["gab"])
    a.plot(est["s"]*A.R_REF*A.ALPHA,0,"x",color="green",ms=10,mew=2,label=L["target"])
    first=True
    for tm in marks:
        i=min(int(round(tm/dt0)),len(t)-1)
        a.plot(along[i],radial[i],"*",ms=13,color="gold",mec="k",zorder=5,label=L["burns"] if first else None); first=False
    a.axhline(0,color="k",lw=.5,ls="--"); a.axhline(A.ALT_MAX-A.H_REF,color="r",lw=.8,ls=":",label=L["ceil"])
    a.set_xlabel(L["along"]); a.set_ylabel(L["altrel"]); a.set_title(L["relt"],fontsize=10); a.legend(fontsize=8)
    a=ax[1]
    a.plot(t/60,alt,color="#c0392b"); a.axhline(450,color="r",ls=":",lw=.8); a.axhline(400,color="k",ls="--",lw=.6)
    a.set_xlabel(L["time"]); a.set_ylabel(L["alt"],color="#c0392b")
    b=a.twinx(); b.plot(t/60,dist,color="#1b4f8a"); b.axhline(1500,color="green",ls="--",lw=.8)
    b.axhspan(1000,2000,color="green",alpha=.06); b.set_ylabel(L["dist"],color="#1b4f8a")
    for tm in marks: a.axvline(tm/60,color="gold",lw=1)
    a.set_title(L["timet"],fontsize=10)
    fig.suptitle(f"{L['strat'][idx]} ({nome}, {h:.0f} km, {d:.0f} km)   |   {L['total']} = {fmt(lang,res['dv_total']*1000)} m/s, "
                 f"{L['dur']} = {res['t_total']/60:.0f} {L['minutes']}",fontsize=11)
    fig.tight_layout(); fig.savefig(f"figs/{arq}_{lang}.png",dpi=160); plt.close(fig)

# ---------------------------------------------------------------- elipses ilustrativas
def painel_elipse(a, lang, rp, ra, partida, gab_ang_deg, titulo, ahead):
    L=T[lang]
    sa=(rp+ra)/2; e=(ra-rp)/(ra+rp); p=sa*(1-e*e)
    cosf=(p/1.0-1)/e; fs=math.acos(cosf)
    if partida=="perigeu":   # f=0 no ângulo 0
        f0=0.0; fc1=fs; fc2=2*math.pi-fs; omega=0.0
    else:                    # apogeu no ângulo 0  (f0=pi)
        f0=math.pi; fc1=2*math.pi-fs; fc2=2*math.pi+fs; omega=-math.pi
    rr=lambda f:p/(1+e*np.cos(f))
    def seg(fa,fb,n=300):
        f=np.linspace(fa,fb,n); r=rr(f); return r*np.cos(f+omega), r*np.sin(f+omega)
    th=np.linspace(0,2*np.pi,500)
    a.fill(0.55*np.cos(th),0.55*np.sin(th),color="#9ec5e8",zorder=1)
    a.text(0,0,L["earth"],ha="center",va="center",fontsize=10,zorder=2)
    a.plot(np.cos(th),np.sin(th),"k--",lw=1,label=L["ref"],zorder=2)
    a.plot(*seg(f0,f0+2*math.pi),color="0.65",lw=1,ls=":",label=L["rest"],zorder=2)
    a.plot(*seg(f0,fc1),color="#e67e22",lw=2.6,label=L["p1"],zorder=4)
    a.plot(*seg(fc1,fc2),color="#2e86c1",lw=2.6,label=L["p2"],zorder=4)
    for f,lab in ((fc1,"1"),(fc2,"2")):
        x,y=rr(f)*math.cos(f+omega),rr(f)*math.sin(f+omega)
        a.plot(x,y,"o",ms=15,mfc="white",mec="k",zorder=6)
        a.text(x,y,lab,ha="center",va="center",fontsize=9,fontweight="bold",zorder=7)
    x0,y0=rr(f0)*math.cos(f0+omega),rr(f0)*math.sin(f0+omega)
    a.plot(x0,y0,"*",ms=18,color="gold",mec="k",zorder=7)
    a.annotate(L["burn1"],(x0,y0),textcoords="offset points",xytext=(48,-22),fontsize=9,arrowprops=dict(arrowstyle="-",lw=0.7,color="0.3"))
    ga=math.radians(gab_ang_deg)
    a.plot(math.cos(ga),math.sin(ga),"s",ms=11,color="#1b4f8a",zorder=6)
    a.annotate(L["gab"],(math.cos(ga),math.sin(ga)),textcoords="offset points",xytext=(-10,-4) if ahead is False else (10,-4),ha="right" if ahead is False else "left",va="center",fontsize=9,color="#1b4f8a")
    # seta de sentido do movimento (anti-horário) sobre a órbita de referência
    ang=math.radians(145); a.annotate("",xy=(1.0*math.cos(ang+0.12),1.0*math.sin(ang+0.12)),xytext=(math.cos(ang),math.sin(ang)),
        arrowprops=dict(arrowstyle="->",lw=1.6,color="k"),zorder=5)
    a.set_aspect("equal"); a.set_xlim(-1.55,1.95); a.set_ylim(-1.45,1.45); a.axis("off")
    a.set_title(titulo,fontsize=10)

def fig_elipses(lang):
    L=T[lang]
    fig,ax=plt.subplots(1,2,figsize=(12,5.6))
    painel_elipse(ax[0],lang,0.80,1.20,"perigeu",-28,L["ca"],True)
    painel_elipse(ax[1],lang,0.76,1.16,"apogeu",+28,L["cb"],False)
    h,l=ax[0].get_legend_handles_labels()
    fig.legend(h,l,loc="lower center",ncol=4,fontsize=9,frameon=False,bbox_to_anchor=(0.5,0.045))
    fig.text(0.5,0.01,L["scale_note"],ha="center",fontsize=9,style="italic")
    fig.subplots_adjust(left=0.01,right=0.99,top=0.90,bottom=0.12,wspace=0.02); fig.savefig(f"figs/fig_elipses_{lang}.png",dpi=160,bbox_inches="tight"); plt.close(fig)

# ---------------------------------------------------------------- ilustração conceitual das 3 estratégias
TC={"en":dict(t=["Strategy 1: 1st crossing","Strategy 2: 2nd crossing","Strategy 3: phasing"],
        ref="400 km orbit (target)",ini="initial orbit",wait="waiting orbit",path="transfer path",dv="burn",
        sub=["burn 1 at the start,\nburn 2 at the 1st crossing","same ellipse, burn 2 at the\n2nd crossing (longer, cheaper)","4 burns: go to a waiting orbit,\nwait, return to 400 km"],
        note="Schematic view from above. Geometry exaggerated for visibility (real transfers have $e<0.012$)."),
    "pt":dict(t=["Estratégia 1: 1º cruzamento","Estratégia 2: 2º cruzamento","Estratégia 3: faseamento"],
        ref="órbita de 400 km (alvo)",ini="órbita inicial",wait="órbita de espera",path="trajetória de transferência",dv="impulso",
        sub=["impulso 1 na partida,\nimpulso 2 no 1º cruzamento","mesma elipse, impulso 2 no\n2º cruzamento (mais longo, mais barato)","4 impulsos: vai para uma órbita de espera,\naguarda e volta aos 400 km"],
        note="Vista esquemática de cima. Geometria exagerada para visualização (as transferências reais têm $e<0{,}012$).")}

def _arco(a,rp,ra,f_a,f_b,omega,**kw):
    sa=(rp+ra)/2; e=(ra-rp)/(ra+rp); p=sa*(1-e*e); f=np.linspace(f_a,f_b,300); r=p/(1+e*np.cos(f))
    a.plot(r*np.cos(f+omega),r*np.sin(f+omega),**kw)

def painel_conceito(a,lang,k):
    L=TC[lang]; th=np.linspace(0,2*np.pi,500); Le=T[lang]
    a.fill(0.55*np.cos(th),0.55*np.sin(th),color="#9ec5e8",zorder=1); a.text(0,0,Le["earth"],ha="center",va="center",fontsize=9)
    a.plot(np.cos(th),np.sin(th),"k--",lw=1,zorder=2)
    def star(x,y,lab):
        a.plot(x,y,"*",ms=17,color="gold",mec="k",zorder=7)
        n=math.hypot(x,y); a.annotate(lab,(x,y),textcoords="offset points",xytext=(24*x/n,24*y/n-4),ha="center",fontsize=9,fontweight="bold")
    def circ(x,y,lab):
        a.plot(x,y,"o",ms=13,mfc="white",mec="k",zorder=6); a.text(x,y,lab,ha="center",va="center",fontsize=8,fontweight="bold",zorder=7)
    if k in (1,2):
        ra,rp=1.16,0.76
        sa=(rp+ra)/2; e=(ra-rp)/(ra+rp); p=sa*(1-e*e); fs=math.acos((p-1)/e)
        fc1,fc2=2*math.pi-fs,2*math.pi+fs; om=-math.pi
        P=lambda f:((p/(1+e*math.cos(f)))*math.cos(f+om),(p/(1+e*math.cos(f)))*math.sin(f+om))
        a.plot(ra*np.cos(th),ra*np.sin(th),color="0.6",ls=":",lw=1)
        fim=fc1 if k==1 else fc2
        _arco(a,rp,ra,math.pi,fim,om,color="#e67e22" if k==1 else "#2e86c1",lw=2.8,zorder=4)
        star(*P(math.pi),"$\\Delta V_1$"); star(*P(fim),"$\\Delta V_2$")
        for f,lab in ((fc1,"1"),(fc2,"2")):
            if f!=fim: circ(*P(f),lab)
    else:
        rw,rs=0.78,1.2
        a.plot(rs*np.cos(th),rs*np.sin(th),color="0.6",ls=":",lw=1)
        a.plot(rw*np.cos(th),rw*np.sin(th),color="#8e44ad",ls="--",lw=1.2)
        # 1) inicio -> espera (apogeu em 0 graus, perigeu em 180)
        _arco(a,rw,rs,math.pi,2*math.pi,-math.pi,color="#2e86c1",lw=2.6,zorder=4)
        # espera de 180 a 300 graus (varias voltas na pratica)
        w=np.radians(np.linspace(180,300,100)); a.plot(rw*np.cos(w),rw*np.sin(w),color="#8e44ad",lw=3,zorder=4)
        a.annotate({"en":"waiting\n(several laps)","pt":"espera\n(várias voltas)"}[lang],(rw*math.cos(math.radians(240)),rw*math.sin(math.radians(240))),
                   textcoords="offset points",xytext=(-34,-26),ha="center",fontsize=8,color="#8e44ad")
        # 3) espera -> 400 km (perigeu em -60 graus, apogeu em 120)
        _arco(a,rw,1.0,0,math.pi,math.radians(-60),color="#e67e22",lw=2.6,zorder=4)
        for ang,r_,lab in ((0,rs,"1"),(180,rw,"2"),(300,rw,"3"),(120,1.0,"4")):
            star(r_*math.cos(math.radians(ang)),r_*math.sin(math.radians(ang)),"$\\Delta V_"+lab+"$")
    a.set_aspect("equal"); a.set_xlim(-1.55,1.55); a.set_ylim(-1.4,1.4); a.axis("off")
    a.set_title(L["t"][k-1]+"\n"+L["sub"][k-1],fontsize=10)

def fig_conceitos(lang):
    L=TC[lang]
    fig,ax=plt.subplots(1,3,figsize=(15,5.6))
    for i in range(3): painel_conceito(ax[i],lang,i+1)
    from matplotlib.lines import Line2D
    h=[Line2D([],[],color="k",ls="--",label=L["ref"]),Line2D([],[],color="0.6",ls=":",label=L["ini"]),
       Line2D([],[],color="#8e44ad",ls="--",label=L["wait"]),Line2D([],[],color="#2e86c1",lw=2.6,label=L["path"]),
       Line2D([],[],marker="*",color="gold",mec="k",ls="",ms=13,label=L["dv"])]
    fig.legend(handles=h,loc="lower center",ncol=5,fontsize=9,frameon=False,bbox_to_anchor=(0.5,0.05))
    fig.text(0.5,0.01,L["note"],ha="center",fontsize=9,style="italic")
    fig.subplots_adjust(left=0.01,right=0.99,top=0.86,bottom=0.12,wspace=0.02)
    fig.savefig(f"figs/fig_conceitos_{lang}.png",dpi=150,bbox_inches="tight"); plt.close(fig)

# ---------------------------------------------------------------- gráficos "tipo GS": vista de cima, altitude, distância
TG={"en":dict(top="Top view (Gabriella fixed)",alt="Altitude of the auxiliary satellite",dist="Distance to Gabriella",
        radnote="radial deviation exaggerated ×{}",lim="altitude limits (350 / 450 km)",ref="400 km",
        t="time [min]",ya="altitude [km]",yd="distance [km]",lo="floor 350 km",hi="ceiling 450 km",dlo="envelope 1000 km",dhi="envelope 2000 km",
        tgt="target 1500 km",start="start",gab="Gabriella",burn="burn",
        sup="{} ({}, {} km, {} km): total $\\Delta V$ = {} m/s, duration = {} min"),
    "pt":dict(top="Vista de cima (Gabriella fixa)",alt="Altitude do satélite auxiliar",dist="Distância até a Gabriella",
        radnote="desvio radial exagerado ×{}",lim="limites de altitude (350 / 450 km)",ref="400 km",
        t="tempo [min]",ya="altitude [km]",yd="distância [km]",lo="piso 350 km",hi="teto 450 km",dlo="envelope 1000 km",dhi="envelope 2000 km",
        tgt="alvo 1500 km",start="início",gab="Gabriella",burn="impulso",
        sup="{} ({}, {} km, {} km): $\\Delta V$ total = {} m/s, duração = {} min")}

def fig_gs(lang,nome,h,d,idx,arq,K=10):
    L=TG[lang]; Ls=T[lang]
    est=A.estado_inicial("atras" if nome=="Beatriz" else "frente",h,d)
    res=[A.analisa_direto(est,1),A.analisa_direto(est,2),A.analisa_faseamento(est)][idx]; assert res["viavel"],res["motivo"]
    tr,marks=traj(est,res["eventos"]); t=tr[:,0];x=tr[:,1];y=tr[:,2]; alt=np.hypot(x,y)-A.R_TERRA
    gx=A.R_REF*np.cos(A.N_REF*t);gy=A.R_REF*np.sin(A.N_REF*t); dist=np.hypot(x-gx,y-gy)
    c,s=np.cos(-A.N_REF*t),np.sin(-A.N_REF*t); rx=x*c-y*s; ry=x*s+y*c
    r=np.hypot(rx,ry); ang=np.arctan2(ry,rx)                     # referencial que gira com a Gabriella
    rp=A.R_REF+K*(r-A.R_REF)                                     # exagero radial
    px,py=-rp*np.sin(ang),rp*np.cos(ang)                         # Gabriella no topo; a órbita é anti-horária, então "atrás" fica à direita
    dt0=t[1]-t[0]; idxs=[min(int(round(m/dt0)),len(t)-1) for m in marks]
    fig=plt.figure(figsize=(15,4.8)); gs=fig.add_gridspec(1,3,width_ratios=[1.5,1,1]); ax=[fig.add_subplot(gs[i]) for i in range(3)]
    a=ax[0]; thc=np.linspace(-0.35,0.05,200)
    for rr,st,col,lab in ((A.R_REF,"--","k",L["ref"]),(A.R_REF-K*50,":","r",L["lim"]),(A.R_REF+K*50,":","r",None)):
        a.plot(-rr*np.sin(thc),rr*np.cos(thc),st,color=col,lw=1,label=lab)
    a.plot(px,py,color="#c0392b",lw=2,zorder=4)
    a.plot(px[0],py[0],"o",color="#c0392b",ms=8,label=L["start"],zorder=5)
    a.plot(0,A.R_REF,"s",color="#1b4f8a",ms=10,label=L["gab"],zorder=5)
    a.plot(px[-1],py[-1],"x",color="green",ms=11,mew=2,label=L["tgt"],zorder=5)
    for i in idxs: a.plot(px[i],py[i],"*",ms=14,color="gold",mec="k",zorder=6,label=L["burn"] if i==idxs[0] else None)
    a.set_aspect("equal"); a.set_xlim(-350,1950); a.set_ylim(6180,7360)
    a.legend(fontsize=7.5,loc="upper center",bbox_to_anchor=(0.5,-0.13),ncol=3,frameon=False); a.set_title(L["top"]+"\n("+L["radnote"].format(K)+")",fontsize=10)
    a.set_yticks([])
    a=ax[1]; a.plot(t/60,alt,color="#c0392b"); a.axhline(350,color="r",ls=":"); a.axhline(450,color="r",ls=":"); a.axhline(400,color="k",ls="--",lw=.6)
    a.axhspan(350,450,color="green",alpha=.06); a.set_ylim(340,460)
    a.text(t[-1]/60,452,L["hi"],ha="right",fontsize=8,color="r"); a.text(t[-1]/60,352,L["lo"],ha="right",fontsize=8,color="r")
    for m in marks: a.axvline(m/60,color="gold",lw=1)
    a.set_xlabel(L["t"]); a.set_ylabel(L["ya"]); a.set_title(L["alt"],fontsize=10)
    a=ax[2]; a.plot(t/60,dist,color="#1b4f8a"); a.axhline(1000,color="r",ls=":"); a.axhline(2000,color="r",ls=":"); a.axhline(1500,color="green",ls="--",lw=.8)
    a.axhspan(1000,2000,color="green",alpha=.06); a.set_ylim(900,2100)
    a.text(t[-1]/60,2020,L["dhi"],ha="right",fontsize=8,color="r"); a.text(t[-1]/60,1010,L["dlo"],ha="right",fontsize=8,color="r")
    for m in marks: a.axvline(m/60,color="gold",lw=1)
    a.set_xlabel(L["t"]); a.set_ylabel(L["yd"]); a.set_title(L["dist"],fontsize=10)
    fig.suptitle(L["sup"].format(Ls["strat"][idx],nome,f"{h:.0f}",f"{d:.0f}",fmt(lang,res["dv_total"]*1000),f"{res['t_total']/60:.0f}"),fontsize=11)
    fig.tight_layout(); fig.savefig(f"figs/{arq}_{lang}.png",dpi=150); plt.close(fig)

# ---------------------------------------------------------------- mapa
def fig_mapa(lang):
    L=T[lang]; dados=pickle.load(open("mapa_dados.pkl","rb")); hs,ds=dados["hs"],dados["ds"]
    cores=["#dddddd","#f4d03f","#e67e22","#2e86c1"]
    fig,axs=plt.subplots(2,2,figsize=(12,8.5)); ext=[ds[0]-50,ds[-1]+50,hs[0]-7.5,hs[-1]+7.5]
    for j,nome in enumerate(("Beatriz","Carolina")):
        cod,dv=dados[nome]
        ax=axs[0,j]; ax.imshow(cod,origin="lower",aspect="auto",cmap=ListedColormap(cores),vmin=0,vmax=3,extent=ext)
        ax.set_title(L["mtitle"].format(nome)); ax.set_xlabel(L["xl"]); ax.set_ylabel(L["yl"])
        ax.axvline(1500,color="k",ls=":"); ax.axhline(400,color="k",ls=":")
        ax=axs[1,j]; im=ax.imshow(dv,origin="lower",aspect="auto",cmap="viridis",extent=ext)
        ax.set_title(L["mdv"].format(nome)); ax.set_xlabel(L["xl"]); ax.set_ylabel(L["yl"]); plt.colorbar(im,ax=ax)
    fig.legend(handles=[Patch(color=c,label=l) for c,l in zip(cores,L["leg"])],loc="lower center",ncol=4)
    fig.tight_layout(rect=(0,0.04,1,1)); fig.savefig(f"figs/fig_mapa_{lang}.png",dpi=130); plt.close(fig)

if __name__=="__main__":
    what=sys.argv[1] if len(sys.argv)>1 else "all"
    for lang in ("en","pt"):
        if what in ("all","elipses"): fig_elipses(lang)
        if what in ("all","estrategias"):
            fig_estrategia(lang,"Beatriz",450,1400,0,"fig_manobra1")
            fig_estrategia(lang,"Beatriz",450,1400,1,"fig_manobra2")
            fig_estrategia(lang,"Beatriz",450,1400,2,"fig_manobra3")
        if what in ("all","mapa"): fig_mapa(lang)
        if what in ("all","conceitos"): fig_conceitos(lang)
        if what in ("all","gs"): fig_gs(lang,"Beatriz",450,1400,1,"fig_gs_exemplo")
