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
- Enregistrer avec le projet le choix des courbes de l'historique (état DPF) : fait avec G.
- Proposer le correctif `patches/dpf-vst3-flush-parameters.patch` à DPF.
- Version macOS (nécessite une machine macOS ou osxcross).

## Propositions, 2e série (2026-11-24)
Classées par intérêt. E retirée ; I faite. **Toutes acceptées le 2026-11-25** (C : valeur de
chaque préréglage laissée à mon choix ; D : changement des valeurs accepté), faites dans l'ordre
H, A, B, C, D, F, G, un commit par point.

A. **Dénormaux** (robustesse, CPU) : sur un silence, l'enveloppe (`follow`) décroît
   exponentiellement vers 0 et passe en dénormaux après ~1 500 échantillons ; sur x86 sans FTZ,
   chaque opération peut coûter 10 à 100 fois plus (pic de CPU dans les fins de morceau). Remède :
   FTZ/DAZ le temps de `run` (`_mm_setcsr`, rétabli en sortie) ou plancher à 1e-20. À mesurer
   d'abord (test : silence après un signal, temps de `run`). Sortie inchangée au-dessus de -400 dB.
   **Fait** (2026-11-25) : FTZ/DAZ le temps de `run` (`ScopedNoDenormals`, état de l'hôte
   rétabli ; rien hors SSE). Mesure simultanée (banc H, ancienne et nouvelle version sur deux
   cœurs, FTZ coupé chez l'appelant) : silence 157 → 53 ns par trame en 1x, 349 → 144 en 2x,
   921 → 356 en 4x ; le silence ne dépend plus de l'état du FPU de l'hôte (rapport coupé/activé
   0,97-1,07 contre 1,5-2,6) ; musique inchangée (bruit). Sortie identique bit à bit (7 préréglages
   × 1x/2x/4x, FTZ coupé), sauf des sorties de l'ordre de 2e-28 (-560 dB) sur un canal de 1e-30.
   Test 9 de `test_dsp` : état du FPU de l'appelant rétabli.
B. **Écoute de la différence** (« Delta ») : bouton qui fait entendre sortie − entrée (retardée de
   la latence), ce que le maximiseur et l'écrêteur retirent ; courant sur ce type de plugin, peu
   de code (l'entrée retardée existe déjà pour Bypass).
   **Fait** (2026-11-25) : paramètre `delta` (après Bypass : seuls les indices des sorties
   changent ; l'état est enregistré par symbole).
   Sortie moins la chaîne linéaire (gain du maximiseur tenu à 1/knee, sans écrêtage), celle-ci
   sous-échantillonnée par son propre filtre : un premier essai avec l'entrée retardée à la
   fréquence de l'hôte laissait en 4x la coupure des filtres (0,33 de crête sur du bruit blanc).
   Calculée seulement quand Delta est actif. Sortie hors Delta inchangée (7 préréglages ×
   1x/2x/4x ; seuls des écarts de 5e-35 sur des sorties de 1e-28). Test 10 de `test_dsp`.
C. **Douceur de l'écrêteur Soft réglable** (corrige le défaut connu de `LISEZMOI.md`) : paramètre
   Knee du Clip (seuil de 60 % aujourd'hui, en dur) ; à 90-95 %, Default n'arrondirait plus ~1 dB
   en permanence. Change le son de Default seulement si on change sa valeur (décision).
   **Fait** (2026-11-25) : paramètre `clip_knee` « Soft knee », 0,1-6 dB sous le plafond (après
   Level match : l'automation des paramètres existants reste en place), lissé comme les autres.
   Valeurs choisies (laissées à mon choix) : Default et Loud master 1 dB (-0,23 dB sur les
   crêtes au plafond au lieu de -0,87), Subtle 2 dB (plus doux, -0,45 dB), Wide crush 4,44 dB
   (son de la 1.1.0 gardé), autres 1 dB (sans effet : Off ou Hard). Défaut du paramètre : 1 dB.
   Test 11.
D. **Sonie selon ITU-R BS.1770** (pondération K) pour Level match et l'écart affiché, au lieu du
   RMS large bande : la comparaison suit mieux l'oreille (le RMS surestime les graves). Affichage
   possible en LU. Change les valeurs affichées et la compensation (décision).
   **Fait** (2026-11-25, accepté par l'utilisateur) : pondération K (deux biquads en double par
   canal, entrée et sortie, coefficients pour toute fréquence d'échantillonnage d'après les
   formules de libebur128), même moyenne exponentielle de ~1,5 s, sans fenêtrage ; affichage en LU.
   Réponse : 50 Hz -3,9 dB, 1 kHz +0,7 dB, 10 kHz +4,0 dB. Test 12.
(E, pistes mono : retirée par l'utilisateur, ne pas la reproposer.)
F. **Comparaison A/B** dans l'interface : deux jeux de réglages, bascule et copie A→B ; avec Level
   match, permet de comparer deux réglages à niveau égal.
   **Fait** (2026-11-25) : A | B et A>B dans l'en-tête (étiquette « PRESET » retirée, boîte des
   préréglages réduite à 196 px) ; jeu inactif enregistré avec le projet par un état DPF
   (`DISTRHO_PLUGIN_WANT_STATE` et `WANT_FULL_STATE`, exigé par DPF avec des programmes : le
   plugin garde la chaîne, le DSP l'ignore). Test 13 (état déclaré, gardé, rendu). Clics non
   essayés (Wine : affichage seul).
G. **Durée de l'historique** au choix (3, 7, 30 s) et enregistrement du choix des courbes avec le
   projet (déjà en piste ci-dessus : à faire ensemble, état DPF).
   **Fait** (2026-11-25) : sélecteur 3 s | 7 s | 30 s sous la légende (à la place de « click:
   show / hide », passé dans la bulle d'aide de l'historique) ; tampon de 30 s (1 201 points),
   les derniers 3, 7 ou 30 s affichés ; durée et courbes : état DPF `history`. Clics non essayés.
H. **Banc de CPU** dans `tests/` (temps par échantillon en 1x/2x/4x, avec et sans signal) : base
   pour A et pour toute optimisation future (règle : pas de gain annoncé sans mesure).
   **Fait** (2026-11-25) : `tests/bench_cpu.cpp` (mode d'emploi en tête du fichier ; hors
   `build.sh`, ~15 s). Default, 1x/2x/4x ; musique, silence après 1 s de musique, Bypass ; FPU de
   l'appelant (FTZ/DAZ coupés) et FTZ/DAZ activés ; meilleur de 5. Première mesure (i7-8665U,
   machine chargée par d'autres processus : valeurs absolues au simple au triple d'un lancement à
   l'autre, rapports stables) : ~45-130 ns par trame stéréo en 1x, ~330-430 en 2x, ~530-650 en
   4x (0,2 à 2,9 % d'un cœur) ; **silence : 1,4 à 2,1 fois plus lent sans FTZ** (dénormaux
   confirmés : l'enveloppe reste bloquée sur le plus petit dénormal, 1,4e-45, car
   1,4e-45 × 29/30 s'arrondit à lui-même) ; Bypass coûte autant que le traitement (il continue
   pour le fondu).
I. **Charte graphique alternative « Ash » (claire) et bouton de bascule** : **faite** le
   2026-11-24 (demandée par l'utilisateur ; mémorisation (b) : préférence de l'utilisateur pour
   toutes les instances, pas avec le projet). Détail : `LISEZMOI.md` § « Interface » ; captures
   `doc/captures/capture_interface.png` (Ember) et `capture_interface_ash.png` (Ash).

## Demandé le 2026-11-25
J. **Liste déroulante des préréglages** (à la place des flèches < >) et **préréglages de
   l'utilisateur** (enregistrer, remplacer, supprimer ; sections FACTORY et USER séparées par un
   trait ; un fichier `.preset` par préréglage, commun à tous les projets et hôtes) : faits.
   Détail : `LISEZMOI.md` § « Interface ». Reste : essai des clics et de la saisie dans un vrai hôte.

K. **Filtre de suréchantillonnage allongé** (demandé le 2026-11-25, après le constat fait en
   rédigeant le manuel) : l'étage 2x passait de 47 à 107 coefficients (le second étage du 4x reste
   à 47). Avant : -0,57 dB à 19 kHz, -2,0 dB à 20 kHz, 19 dB de rejet à 24,1 kHz ; après :
   -0,006 dB à 20 kHz, ≥ 80 dB au-delà de 24,2 kHz (hôte à 44,1 kHz). Latence +30 échantillons
   (2x : 38 → 68, 4x : 50 → 80). CPU, mesures simultanées sur deux cœurs, deux tours croisés
   (machine peu chargée) : 2x 113 → 164 ns par trame (+45 %), 4x 224 → 281 ns (+25 %), soit
   0,5 → 0,7 % et 1,0 → 1,2 % d'un cœur à 44,1 kHz ; 1x +4 à 8 % (~2 ns, les filtres n'y servent
   pas : disposition du code). Test 4 de `test_dsp` : réponse à 18, 19, 20 kHz en 2x et 4x.

Version : reste 1.1.0 (décision du 2026-11-25 : le logiciel n'a jamais été utilisé).

## Demandé le 2026-11-26 (après la release 1.1.0)
L. **Courbe « maximizer » dans l'historique** : crête à l'entrée de l'écrêteur (après maximiseur,
   Mix et Gain), indépendante de l'écrêteur, de Delta, de Level match et de Bypass (test 8 de
   `test_dsp`) ; la courbe *output* reste la sortie du greffon (avec Delta : le niveau de la
   différence, ce qu'on entend). Sortie DSP `hist_max` ajoutée en dernier (indices des
   paramètres inchangés). Demande un nouveau numéro de version à la prochaine release.

## Écarté
- **Mode limiteur** (relâchement long séparé de l'anticipation, attaque lissée, détection des crêtes
  entre échantillons) : refusé par l'utilisateur le 2026-11-24. Ne pas le reproposer.
