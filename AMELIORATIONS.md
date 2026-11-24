# Brimstone Maximizer : améliorations

Proposées le 2026-11-24, toutes jugées pertinentes par l'utilisateur, faites le même jour
(version 1.1.0) sauf mention. Détails : `LISEZMOI.md`.

## Son
1. **Plafond de sortie** : fait. Ceiling -12..0 dB ; Clip Off / Hard / Soft ; écrêtage de sûreté
   après le sous-échantillonnage.
2. **Suréchantillonnage 1x/2x/4x** : fait (demi-bandes FIR 47 coefficients, Kaiser β = 8).
3. **Decay en millisecondes** : fait, bouton SMP/MS ; l'unité d'origine reste disponible.
4. **Drive** : fait (0..24 dB, signal traité seulement).
5. **Mode Mid/Side** : fait (Stereo : Link, L/R, M/S).
6. **Changement de Decay sans clic** : fait, deux détecteurs fondus en 5 ms (un premier essai,
   glissement progressif de l'anticipation, créait des dépassements : abandonné).

## Interface
7. **Historique de la réduction de gain** : fait (~7 s, entrée et réduction).
8. **Saisie au clavier** (clic droit) et bulle d'aide au survol : faits.
9. **Préréglages d'usine** : faits (6, dont « Original (swh) »), exposés aussi à l'hôte.
10. **Écoute à niveau égal** : fait (Level match + écart de sonie affiché).

## Hôte et diffusion
11. **Paramètre Bypass** déclaré à l'hôte : fait.
12. **Autres formats** : CLAP (Windows, Linux), LV2 et VST3 Linux faits ; VST2 écarté (licence) ;
    macOS non compilable ici.
13. **Validation** : validateur de Steinberg 47/47 (Linux et Windows sous Wine), après correctif
    de DPF. **Reste** : essai dans un vrai logiciel audio Windows.
14. **Installateur Windows** : fait (`installer/brimstone.nsi`, NSIS : VST3 + CLAP en option,
    désinstalleur, licence) → `bin/Brimstone-Maximizer-1.1.0-setup.exe` ; installation et
    désinstallation silencieuses vérifiées sous Wine. **Reste** : essai sur un vrai Windows.

## Ajouts demandés ensuite (2026-11-24)
- Préréglage **Clipper** (Mix 0 %, Gain +3 dB, Hard à -0,3 dB, 4x).
- **Output renommé Gain** ; interface en trois sections dans l'ordre du signal, maximiseur grisé à
  Mix 0 %, bulles d'aide partout, courbe de transfert avec Mix.
- **Historique** : réduction de l'écrêteur mesurée (vu-mètre CLIP), réduction du maximiseur à 0
  quand Mix = 0 % ; courbes input, output, maximizer GR, clipper GR, out - in sur une même bande,
  traits fins, masquables.

## Pistes nouvelles (non demandées)
- Enregistrer avec le projet le choix des courbes de l'historique (état DPF).
- Proposer le correctif `patches/dpf-vst3-flush-parameters.patch` à DPF.
- Version macOS (nécessite une machine macOS ou osxcross).

## Propositions, 2e série (2026-11-24, en attente de décision)
Classées par intérêt. A à H : non commencées (décision de l'utilisateur) ; E retirée ; I faite.

A. **Dénormaux** (robustesse, CPU) : sur un silence, l'enveloppe (`follow`) décroît
   exponentiellement vers 0 et passe en dénormaux après ~1 500 échantillons ; sur x86 sans FTZ,
   chaque opération peut coûter 10 à 100 fois plus (pic de CPU dans les fins de morceau). Remède :
   FTZ/DAZ le temps de `run` (`_mm_setcsr`, rétabli en sortie) ou plancher à 1e-20. À mesurer
   d'abord (test : silence après un signal, temps de `run`). Sortie inchangée au-dessus de -400 dB.
B. **Écoute de la différence** (« Delta ») : bouton qui fait entendre sortie − entrée (retardée de
   la latence), ce que le maximiseur et l'écrêteur retirent ; courant sur ce type de plugin, peu
   de code (l'entrée retardée existe déjà pour Bypass).
C. **Douceur de l'écrêteur Soft réglable** (corrige le défaut connu de `LISEZMOI.md`) : paramètre
   Knee du Clip (seuil de 60 % aujourd'hui, en dur) ; à 90-95 %, Default n'arrondirait plus ~1 dB
   en permanence. Change le son de Default seulement si on change sa valeur (décision).
D. **Sonie selon ITU-R BS.1770** (pondération K) pour Level match et l'écart affiché, au lieu du
   RMS large bande : la comparaison suit mieux l'oreille (le RMS surestime les graves). Affichage
   possible en LU. Change les valeurs affichées et la compensation (décision).
(E, pistes mono : retirée par l'utilisateur, ne pas la reproposer.)
F. **Comparaison A/B** dans l'interface : deux jeux de réglages, bascule et copie A→B ; avec Level
   match, permet de comparer deux réglages à niveau égal.
G. **Durée de l'historique** au choix (3, 7, 30 s) et enregistrement du choix des courbes avec le
   projet (déjà en piste ci-dessus : à faire ensemble, état DPF).
H. **Banc de CPU** dans `tests/` (temps par échantillon en 1x/2x/4x, avec et sans signal) : base
   pour A et pour toute optimisation future (règle : pas de gain annoncé sans mesure).
I. **Charte graphique alternative « Ash » (claire) et bouton de bascule** : **faite** le
   2026-11-24 (demandée par l'utilisateur ; mémorisation (b) : préférence de l'utilisateur pour
   toutes les instances, pas avec le projet). Détail : `LISEZMOI.md` § « Interface » ; captures
   `capture_interface.png` (Ember) et `capture_interface_ash.png` (Ash).

## Écarté
- **Mode limiteur** (relâchement long séparé de l'anticipation, attaque lissée, détection des crêtes
  entre échantillons) : refusé par l'utilisateur le 2026-11-24. Ne pas le reproposer.
