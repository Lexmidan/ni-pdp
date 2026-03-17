# Úloha SQX: skládání quatromin s maximem
## Popis
3 < = a, b < = 20, rozměry obdélníkové herní desky S, kde ab>=20 a kde každé políčko je ohodnoceno celým číslem z intervalu < -100,-1> a <1,100>.
P={T,L} množina čtveřic kostek (quatromin). Vzorová quatromina se označují písmeny I, L, O, T, Z. Podobně jako v tetrisu, i zde se quatromina mohou otáčet a zrcadlit. Cílem úlohy je umístit quatrominy z množiny P na herní desku S tak, aby se navzájem nepřekrývaly a aby součet ohodnocení políček, která NEJSOU pokryta umístěnými quatrominy, byl co největší.
**V této úloze budeme pracovat pouze s quatrominy tvaru T a L.**

## Vstup
Vstupní data jsou ve formátu:
```
a b
s_11 s_12 ... s_1b
s_21 s_22 ... s_2b
...
s_a1 s_a2 ... s_ab
```

## Výstup

Maticový popis výsledného řešení pokrytí, kde každé umístěné quatromino bude popsáno čtveřicí slabik [T|L]<index>, kde <index> je unikátní pořadové číslo umístěného quatromina. U nepokrytých políček vypište jejich ohodnocení.

Například maticový popis řešení pro desku 4x4 může vypadat takto:
 T1  T1  T1  7
  1  T1  L1  9
  2   5  L1  4
  3   4  L1  L1


# Kód
`src/main.cpp` obsahuje implementaci řešení úlohy SQX: skládání quatromin s maximem.

Pouziti:
```
  sqx <vstupni_soubor>
```
Vstupni soubor se hleda ve slozce `mapb/`, vystup se uklada do `mapsol/`.