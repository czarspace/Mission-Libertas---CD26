# Calcula (uma vez) os dados do mapa de viabilidade e salva em mapa_dados.pkl
import pickle, numpy as np
import analisar_manobras as A
hs=np.arange(300,451,15.0); ds=np.arange(1000,2001,100.0)
out={"hs":hs,"ds":ds}
for nome in ("Beatriz","Carolina"):
    cod=np.zeros((len(hs),len(ds)));dv=np.full((len(hs),len(ds)),np.nan)
    for i,h in enumerate(hs):
        for k,d in enumerate(ds):
            r=A.analisar(nome,h,d,verbose=False,forcar=True)
            v=[x["viavel"] for x in r]
            cod[i,k]=(3 if v[2] else 1) if (v[0] or v[1]) else (2 if v[2] else 0)
            c=[x["dv_total"]*1000 for x in r if x["viavel"]]
            if c: dv[i,k]=min(c)
    out[nome]=(cod,dv)
pickle.dump(out,open("mapa_dados.pkl","wb"))
print("ok")
