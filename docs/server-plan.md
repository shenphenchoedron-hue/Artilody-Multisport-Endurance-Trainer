# Artilody – plan for ghost rider/løber og netværksserver

Status: trin 0–6 implementeret (release 2.0.0). Installation: se
`docs/Artilody-Network-Server-Manual.pdf` – XAMPP (Windows/macOS: `setup.php`)
eller `install.sh` (Debian/Ubuntu/Raspberry Pi).
Oprettet: 2026-11-12

## Formål

Appen skal **fortsat kunne køre helt selvstændigt** på én Raspberry Pi. Derudover
skal den kunne sættes op til at bruge en lille server på det lokale netværk, så:

- brugere, historik og avatarer administreres ét sted og kan bruges på alle
  træningsstationer (én Pi pr. træningsmaskine),
- GPX-ruter og videoer uploades til serveren og hentes ned på den enkelte station,
- træningsdata kan analyseres fra en anden computer via et web interface,
- man på sigt kan køre/løbe sammen på samme GPX-rute.

## Grundprincip: "lokal først, synk bagefter"

- Appen gemmer **altid** lokalt først (SQLite, som i dag). Intet går tabt, hvis
  wifi svigter.
- Ny indstilling: **Server: Fra / adresse**. Standard er *Fra* – så opfører appen
  sig præcis som i dag.
- Med server slået til synkroniseres data i baggrunden. Hver træning har et flag
  "sendt / ikke sendt"; usendte træninger ligger i en kø, til forbindelsen er der.
- Serveren er "chef" for brugerprofiler. Træninger tilføjes kun (ændres aldrig),
  så der opstår stort set ingen konflikter.

### Serveren er kun tændt under træning

Serveren tændes kun, når der trænes. Derfor:

- Stationerne må aldrig vente på serveren. De starter op med den lokale kopi af
  brugere og filer og forsøger løbende (fx hvert 30. sekund) at finde serveren.
  Når den dukker op, opdateres brugerlisten, og sende-køen tømmes automatisk.
- Rækkefølgen af, hvad der tændes først, er ligegyldig.
- Appen viser diskret serverstatus (forbundet / ikke forbundet / X træninger
  venter på at blive sendt).
- Serveren skal **lukkes korrekt ned** (tænd/sluk-knappen eller nedlukning via
  web interfacet) – ikke ved at trække strømstikket, da databasen ellers kan
  tage skade.
- Ingen natlige opgaver: backup og hentning af videoer sker, mens serveren er
  tændt (backup fx automatisk ved opstart eller nedlukning).
- Web interfacet (administration og analyse) kræver, at serveren er tændt.

## Overblik over trin

| Trin | Indhold | Kræver server |
|------|---------|---------------|
| 0 | Ghost rider / ghost løber på GPX-ruter | Nej |
| 1 | Forberedelse i appen (unikke ID'er, serverindstilling) – implementeret | Nej |
| 2 | Opsætning af serveren – grundopsætning implementeret | – |
| 3 | Administration af brugere – implementeret | Ja |
| 4 | Mediebibliotek (GPX og video) – implementeret | Ja |
| 5 | Synk af træninger + analyse på web – implementeret | Ja |
| 6 | Køre/løbe sammen på GPX-ruter – implementeret | Ja |

Hvert trin skal være færdigt og fungere, før det næste påbegyndes.

---

## Trin 0 – Ghost rider / ghost løber

**Status: implementeret (2026-11-12).** `src/core/RouteGhost.*`, tabel
`route_traces` (database v11), 3D-tegning i `Route3DScene` (`kGhostLaneM`),
UI: `GhostPicker.qml` og `GhostBadge.qml`. Tests: `tst_routeghost`,
`tst_gpxride` (`ghostRacesTheEarlierRide`), `tst_migration`.

Kør eller løb mod en tidligere tur på samme GPX-rute – din egen eller (med
serveren, trin 5) en anden brugers.

### Hvad findes allerede

- Ro-siden har Ghost Race (`qml/pages/RowingPage.qml`): YOU / GHOST, afstand
  foran/bagud og fremdriftsbjælke. Samme visning genbruges.
- 3D-motoren kan placere en figur hvor som helst på ruten
  (`src/route3d/ghost_motion.h`, `ghostPose`), og har både cyklist
  (`GhostRider`) og løber (`GhostRunner`).
- Asfalten er ca. 6 m bred (3 m til hver side af midterlinjen,
  `Route3DScene.cpp`), og ens egen avatar kører i dag 1,4 m fra midten.

### Hvad skal laves

1. **Optag position på ruten:** under en GPX-tur gemmes tilbagelagt distans pr.
   sekund (sammen med de øvrige samples), så turen kan afspilles igen.
2. **Vælg ghost:** før start vælges "Ingen ghost", "Bedste tur" eller "Seneste
   tur" på den valgte rute (samme rute-fil og samme retning).
3. **Afspil ghost:** ghostens distans slås op ud fra den forløbne tid og tegnes
   som en ekstra avatar i 3D-scenen.
4. **Visning:** "+/– m foran/bagud" og fremdriftsbjælke som på ro-siden.
5. **Både cykel (GPX Ride) og løb** (løbesiden med GPX-rute).

### Placering af ghosten på vejen

Ghosten må ikke ligge oven i ens egen avatar, når man kører med samme fart.

- **Fast sidelæns forskydning:** ghosten kører i sin egen "bane" ca. 1,0–1,2 m
  inde mod midten af vejen i forhold til ens egen avatar. Der er god plads på
  den 6 m brede asfalt, og de to figurer kan aldrig overlappe.
- **Blød overgang (valgfrit):** når ghosten er langt væk (fx mere end 20 m),
  kan den glide ind i samme linje som en selv, så den ses tydeligt foran på
  vejen. Når afstanden bliver mindre, glider den ud i sin egen bane igen.
  Bevægelsen udglattes over et par sekunder, så den ikke "hopper".
- **Udseende:** halvgennemsigtig og/eller i en lys "spøgelsesfarve", så man
  aldrig er i tvivl om, hvem der er hvem.
- **Når ghosten er bagved og uden for kameraets synsfelt:** vises kun som tal
  ("Ghost 35 m bagud") og som prik på fremdriftsbjælken.

### Pacer (W/kg)

**Status: implementeret (2026-11-12)** – GPX Ride. Egen tænd/sluk-knap
("Pacer", `RoutePacer`); W/kg justeres med − / + (0,5–8,0, gemmes), også
undervejs (i højre panel og på pacer-badgen, også i fuldskærm): sporet beholdes
op til nu, og resten simuleres igen fra pacerens sted og fart. Tænd/sluk
låses under turen. Kun én modstander ad gangen: tændes pacer, slås ghost fra, og
vælges en ghost, slås pacer fra. Turen simuleres med
`RlvSession::pacerTrace()` ved start (sværhedsgrad, loop, grus og vægt som de
er da). Test: `tst_gpxride` (`pacerHoldsWattsPerKilo`).

En "pacer" på GPX-ruten, der kører med et fast antal W/kg (fx 2,5 W/kg).
Farten beregnes ud fra rytterens vægt og rutens stigning med samme fysik som
ens egen tur. Pacer-figuren tegnes med samme ghost-teknik, men orange og i
banen på den anden side af midterlinjen, med egen badge og orange prik på
højdeprofilen. W/kg vises også i fuldskærms-HUD'en.

---

## Trin 1 – Forberedelse i appen

**Status: implementeret (2026-11-12).** Database v12: kolonnen `uuid` (tilfældig
UUID v4, unik index) på `profiles`, `rides`, `workouts` og `route_traces`.
Eksisterende rækker får en ved opgraderingen, nye rækker får en af en trigger i
databasen (ingen INSERT skal kende til den). Løbenumrene (`id`) bruges
fortsat lokalt; `uuid` er til serveren. Stationen har sit eget ID
(`Database::stationId()`, indstilling `station/id`). Indstillingen
**Server** (`ServerSettings`, QML `Server`) ligger i Settings: Fra/Til og
adresse (`server/enabled`, `server/address`; navn, IP, evt. `:port` eller en
fuld `http(s)://`-adresse), som valideres og vises som URL. Tests:
`tst_migration` (`upgradesV11ToV12Uuids`), `tst_serversettings`.
Videoer og GPX-ruter er filer (ikke rækker i databasen); de identificeres
i trin 4 (fx med rutens fingeraftryk `route_key` / filens hash).

- **Unikke ID'er (UUID)** på brugere, træninger, ruter og videoer i den lokale
  database. I dag bruges løbenumre (1, 2, 3 …), som vil kollidere, når flere
  stationer laver data hver for sig.
- Automatisk opgradering af eksisterende database – intet må gå tabt.
- Ny indstilling: **Server: Fra / adresse** (standard: Fra).

Resultat: appen virker præcis som før, men er klar til en server.

## Trin 2 – Opsætning af serveren

**Status: grundopsætning implementeret (2026-11-12).** Server: Pi 500+ på
`192.168.8.10` (wifi, fast IP), Debian 13, Apache 2.4, PHP 8.4, MariaDB 11.8.
SSH med nøgle fra Mac'en (`lone@192.168.8.10`).

- Kode i `server/`: `lib/bootstrap.php` (config, database, JSON),
  `public/api/ping.php` (svar + registrering af stationen i tabellen
  `stations`), `public/index.php` (statusside med stationer), `schema.sql`.
- `server/deploy.sh` (køres på Mac'en) kopierer koden til Pi'en og kører
  `server/install.sh` dér. Installationen kan køres igen uden datatab:
  - koden lægges i `/var/www/artilody` (kun `public/` vises på nettet),
  - databasen `artilody` med egen bruger, adgangskoden i
    `/etc/artilody/config.php` (kun root og Apache kan læse den, ikke i git),
  - Apache-sitet `artilody` erstatter standardsiden,
  - MariaDB lytter kun på `127.0.0.1` (port 3306 er lukket for netværket),
  - backup af databasen ved opstart og korrekt nedlukning
    (`artilody-backup.service`, de nyeste 30 i `/var/backups/artilody`).
- App: `ServerConnection` (QML `ServerStatus`) spørger `api/ping.php` hvert
  30. sekund, når Server er slået til. Settings viser forbundet/ikke
  forbundet og har "Check now". Test: `tst_serverconnection`.

macOS (Sequoia og nyere): programmer skal have tilladelse til **Lokalt
netværk** (Systemindstillinger › Anonymitet og sikkerhed › Lokalt netværk),
ellers afvises forbindelsen med "Host unreachable" / `ERR_ADDRESS_UNREACHABLE`
– både appen og browseren. Appen spørger kun, når den er signeret (CMake
signerer den lokale build ad hoc) og har `NSLocalNetworkUsageDescription` i
`Info.plist`. Raspberry Pi-stationerne (Linux) har ikke denne regel.

Nedluknings-knappen kom med login i trin 3 (`/admin/server.php`). Mangler
stadig: opstramning af phpMyAdmin (åben for hele netværket).

Stationernes API (POST til `api/*.php`) kræver headeren `X-Artilody-Station`
(appen) eller `X-Artilody-Web` (web interfacets egne sider): en særlig header
får browseren til at spørge serveren først, og det godkendes aldrig – så en
hjemmeside, man har åben på netværket, kan ikke sende data til serveren.

- **Hardware:** Raspberry Pi 500+ (Pi 5-indmad, 16 GB RAM, indbygget 256 GB
  NVMe-SSD). Tilsluttes routeren med netværkskabel og køres uden skærm (SSH og
  web interface). Ved behov for mere videoplads tilføjes en ekstern USB-SSD.
  (Alternativt en Pi 5 med NVMe-SSD – ikke SD-kort.)
- **Software:** Raspberry Pi OS, Apache, PHP og MariaDB ("MySQL").
- **Faste adresser:** DHCP-reservation i routeren for serveren og alle
  træningsstationer. Evt. også navnet `artilody-server.local`.
- **Kode:** serverkoden ligger i projektet i en mappe `server/` (i git).
- **Backup:** automatisk backup af databasen ved opstart/nedlukning (fx til en
  USB-nøgle eller en anden computer). Medie-mapper sikkerhedskopieres manuelt
  af og til, da de fylder meget og sjældent ændres.
- **Nedlukning:** knap i web interfacet til korrekt nedlukning af serveren.
- **Sikkerhed:** serveren er kun tilgængelig på det lokale netværk.

## Trin 3 – Administration af brugere

**Status: implementeret (2026-11-12).** Beslutninger: profiler kan rettes
både på stationen og i web interfacet (den nyeste ændring vinder); brugeren
vælges på stationen ved navn (ingen pinkode); stationens eksisterende
profiler sendes til serveren første gang; web interfacet kræver en
admin-adgangskode (`sudo artilody-admin-password`).

- Server: tabellerne `users` (med `rev`-tæller og "tombstones" for slettede),
  `pictures` (efter sha1), `counters`; `meta.server_id`. `lib/users.php`
  (validering som appens `Profile::isValid()`, nyeste ændring vinder, stationens
  ur korrigeres). API: `api/users.php` (GET `?since=rev`, POST ændringer),
  `api/picture.php`; `ping.php` sender `serverId` og `usersRev`. Web:
  `/admin/` (login, brugerliste, ret/opret/slet, profilbillede, 3D-figurernes
  farver) og `/admin/server.php` (Luk ned / Genstart via `artilody-power`,
  sudoers kun for den kommando). Test: `server/tests/test_users.php`.
- App: database v13 (`profiles.updated_ms`, `dirty`, `picture_hash`, tabellen
  `profile_tombstones`). `UserSync` (QML `ProfileSync`) sender ændringer
  (billeder først) og henter serverens ændringer, hver gang serveren svarer;
  intet hentes under en tur; en profil med ture slettes ikke lokalt.
  Billeder fra serveren gemmes i `profile-pictures/` ved databasen. En ny
  stations startprofil sendes ikke, men fjernes, når serverens brugere kommer.
  Settings viser profilernes status. Tests: `tst_usersync`, `tst_migration`.

Oprindelig plan – server:

- Web interface: opret/ret brugere – navn, vægt, FTP, profilbillede og
  3D-avatarens udseende (cykel, løb, ro).
- API (PHP, JSON): hent brugerliste, hent én bruger, hent profilbillede.

Oprindelig plan – app:

- Med server slået til hentes brugerlisten og avatarer ved opstart.
- Lokal kopi gemmes, så stationen virker, selvom serveren er nede.

## Trin 4 – Mediebibliotek (GPX og video)

**Status: implementeret (2026-11-12).** Beslutninger: upload både i web
interfacet og fra stationen ("Send to Server"); GPX-ruter hentes automatisk,
videoer manuelt ("Download"); en lokal fil overskrives aldrig (en rettet rute
med samme navn er "conflict" og kan hentes som "<navn> (server).gpx").

- Server: tabellerne `media` (tombstones, `rev`-tæller `media`) og
  `media_files`; filerne ligger efter sha1 i `/var/lib/artilody/blobs` og
  serveres af Apache på `/blobs/<sha1>` (med ranges, så en afbrudt hentning
  fortsætter). Upload i bidder på 8 MB (`api/upload.php`: start / chunk /
  finish), så videoer på flere GB virker – også fra browseren. `api/media.php`
  (GET `?since=rev`, POST nyt element; samme filer = samme element).
  `ping.php` sender `mediaRev`. Web: `/admin/media.php` (upload, omdøb, slet;
  slettede filers blobs fjernes, når intet andet bruger dem). Blobs er ikke
  med i database-backuppen. Test: `server/tests/test_media.php`.
- App: database v14 (`server_media`: serverens liste og hvor elementet ligger
  lokalt). `MediaSync` (QML `MediaLibrary`): henter listen, når `mediaRev`
  ændres; genkender lokale filer (GPX efter indhold, video efter navn og
  størrelse); henter til de eksisterende biblioteksmapper (videoen først,
  .gpx sidst; `.part` under hentning); intet hentes automatisk og ingen
  genscanning under en tur; én overførsel ad gangen. UI: biblioteksmenuen i
  GPX/Video Ride › "Server Library…" (dialog med Download / On this station /
  Download copy og fremskridt) og "Send to Server"; status i Settings.
  Tests: `tst_mediasync`, `tst_migration`.

### Workouts og træningsplaner i mediebiblioteket

**Status: implementeret (2026-11-14).** Beslutninger: workouts hentes
automatisk og lægges i workout-biblioteket (en workout, der allerede findes,
lægges ikke ind igen); træningsplaner hentes, når man beder om det, og
importeres i kalenderen fra en valgt startdato.

- Server: to nye `kind` i `media`: `workout` (én `.awt` / `.json` / `.zwo` –
  de formater appens `workoutio::parseFile` læser) og `plan` (én `.awplan`).
  Web: `/admin/media.php` har "Workout" og "Training plan" ved upload. Test:
  `server/tests/test_media.php`.
- App: `MediaSync` gemmer serverens workout- og plan-filer i `server-files/`
  ved databasen (`<kind>/<uuid>.<endelse>`). En hentet workout gives til
  `WorkoutLibrary::importFromServer()` (stille: valg og besked bevares,
  dubletter springes over). En plan hentes med `importPlan()` og åbnes i
  kalenderens import-dialog (`planReady`). Det, stationen selv sender, gemmes
  som kopi og hentes ikke tilbage. Serverens `uuid` og filtype indgår i
  stien og godkendes kun, når de er helt som forventet. En station, der
  opdateres fra en ældre version (som sprang de nye typer over), henter
  listen forfra én gang (`server/mediaKinds`). Hver afsendelse får sin egen
  fil i `server-files/outgoing/`, som slettes, når den er på serveren.
- UI (ingen ekstra knapper): når Server er slået til, går de eksisterende
  Import/Export til serveren i stedet for til harddisken. Workouts › Import
  åbner serverens workouts ("Add to Workouts" lægger en ind igen, fx efter
  sletning; man får besked, når en workout tilføjes eller allerede findes);
  Export › "Send selected workout / all my workouts to the server".
  Calendar: "Import plan…" (Add-dialogen) og Plans › "Import a plan…" åbner
  serverens planer (dag 1 på den valgte dag); Plans › "Export weeks as a
  plan…" › "Send to Server". Med Server slået fra: filer som før.
  Tests: `tst_mediasync` (`workoutsByThemselvesPlansWhenAsked`,
  `workoutAndPlanUploads`, `workoutSuffixesMatchTheServer`), `tst_workoutio`
  (`importFromServerIsQuiet`).

Oprindelig plan – server:

- Upload af GPX- og videofiler via web interfacet.
- API: liste over filer (navn, størrelse, type, dato) og download af en fil.

Oprindelig plan – app:

- Bibliotekvisning: "på denne Pi" eller "Hent".
- Filer gemmes i de eksisterende mapper (`~/Documents/Artilody GPX Routes` og
  `~/Documents/Artilody Videos`), så resten af appen er uændret.
- Videoer hentes ned (ikke streames), fordi afspilningshastigheden følger
  wattene, og streaming over wifi vil få videoen til at hakke.

## Trin 5 – Synk af træninger og analyse

**Status: implementeret (2026-11-12).** Beslutninger: hele historikken sendes
første gang; stationerne henter de andre stationers træninger fuldt ud (graf,
omgange, ghost-spor); ghost kan køre mod andre brugeres ture på samme rute
("Best"/"Latest" er stadig ens egne); en træning slettet på én station
slettes overalt; analysen på web kan ses uden login (kun læsning).

- Server: tabellen `rides` (ét felt pr. nøgletal, `samples` som gzip'et JSON,
  `laps`, ghost-sporet, `bests` = bedste 5 s / 1 / 5 / 20 / 60 min watt,
  tombstones, `rev`-tæller `rides`). `lib/rides.php`, `api/rides.php` (GET
  `?since=rev` i dele á 5000, GET `?uuid=` hele turen, POST ture og
  sletninger); `ping.php` sender `ridesRev`. Web: `/training.php` (brugere;
  pr. bruger: 7/28/365 dage, timer og TSS pr. uge, FTP over tid, bedste watt,
  træningsliste) og `/ride.php` (nøgletal, graf med watt/mål/puls/kadence/fart,
  zoner, bedste watt, omgange); grafer i `assets/charts.js` (ingen
  biblioteker). Test: `server/tests/test_rides.php`.
- App: database v15 (`rides.synced`, `rides.station`, tabellen
  `ride_tombstones`). `RideSync` (QML `RideSyncStatus`) sender nye ture (i
  bunker) og sletninger, når serveren svarer, og henter de andre stationers
  ture ind i Historik (vist med "on <station>"); en tur for en profil, som
  stationen ikke har endnu, hentes, når profilen kommer. Intet hentes under
  en tur. `RouteGhost` viser andre profilers ture på ruten som valg ("Anne ·
  …"). Status i Settings. Tests: `tst_ridesync`, `tst_migration`.

Oprindelig plan – app:

- Træninger lægges i en sende-kø og sendes, når der er forbindelse.
- Historikken på en station viser brugerens træninger fra alle maskiner.
- Ghost rider kan bruge ture fra andre stationer – og fra andre brugere.

Oprindelig plan – server:

- Web interface med grafer og analyse (watt, puls, kadence, udvikling over tid).

## Trin 6 – Køre/løbe sammen på GPX-ruter

**Status: implementeret (2026-11-12).** Beslutninger: alle på samme rute
(GPX Ride / løbebåndsrute med samme rutenøgle) ser automatisk hinanden, når
serveren er tændt; hver vises, hvor den faktisk er på ruten; ingen fælles
start.

- Server: tabellen `live` og `api/live.php` (POST hvert sekund: egen
  position, mens man kører; svar: de andre på samme rute, set inden for 5 s;
  rækker ældre end 1 min fjernes).
- App: `LiveRide` (én for GPX Ride, én for Treadmill Run; QML-sessionens
  `live`) spørger hvert sekund, flytter de andres position frem med deres fart
  for svarets alder og sorterer efter afstand. Op til 6 vises i 3D
  (`Route3DScene::setLive`), hver i egen bane og farve (næsten uigennemsigtig),
  og i panelet "Riding with" (navn, +/− meter, watt eller fart). Test:
  `tst_liveride`.

Oprindelig plan:

- Hver station sender sin position på ruten ca. hvert sekund til serveren og
  henter de andres.
- De andre vises som avatarer i 3D-ruten – samme teknik som ghosten, blot med
  live-data. Hver deltager får sin egen bane på vejen (som ghosten i trin 0).
- Til en husstand er simpel HTTP-polling mod PHP nok. Til en klub bør der
  tilføjes en lille realtidstjeneste (WebSocket) ved siden af Apache.
- Kun på GPX-ruter, ikke oven på video.

---

## Åbne beslutninger

Tages stilling til, når det relevante trin nås:

- [ ] Trin 0: Skal ghosten glide ind i egen linje på lang afstand, eller altid
      køre i sin egen bane?
- [x] Trin 0: Valgmuligheder for ghost – "Ingen", "Bedste", "Seneste" og
      enhver tidligere tur fra listen.
- [x] Trin 3: Pinkode pr. bruger på stationerne, eller blot vælge navn?
- [x] Trin 3: Må profilen rettes på stationen, eller kun via web interfacet?
      (Kun web er enklest.)
- [x] Trin 4: Hentes videoer kun manuelt, eller automatisk i baggrunden, når
      stationen finder serveren?
