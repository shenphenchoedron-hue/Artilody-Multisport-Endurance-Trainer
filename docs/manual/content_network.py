"""Text of the Artilody Network Server Manual (make_manual.py --network).

How to set up the Artilody server (server/ in the repository; the release's
artilody-server-<version>.zip) on Windows and macOS (XAMPP), Debian, Ubuntu and
Raspberry Pi OS (install.sh), and connect the training computers to it. Written
for readers who are not computer experts: every step is spelled out.
Markup is ReportLab paragraph markup, as in content.py.
"""

from content import C, K, L, UI


def B(text):
    """A button or menu item to click."""
    return f'<b>{text}</b>'


def build(s, env):
    NET, REPO, V = env["NETSHOTS"], env["REPO"], env["VERSION"]
    RELEASES = REPO + "/releases/latest"
    ZIP = f"artilody-server-{V}.zip"
    # (Windows paths: no backslashes inside f-string braces on Python 3.9)
    WP0 = C('C:\\Program Files')
    WP1 = C('C:\\xampp')
    WP2 = C('C:\\xampp\\apache\\bin\\httpd.exe')
    WP3 = C('C:\\xampp\\htdocs')
    WP4 = C('C:\\xampp\\htdocs\\artilody')
    WP5 = C('C:\\xampp\\xampp-control.exe')

    # =====================================================================
    s.chapter("Before you start", new_page=True, lead=
              "This manual shows, step by step, how to connect several Artilody training computers to one "
              "server in your home or club, so that they share riders, routes, videos and training. Nothing "
              "here is needed to train: without a server every Artilody computer works on its own, exactly as "
              "before.")

    s.section("What the server does")
    s.p("A <b>server</b> is simply one computer in your home that the other computers talk to. In this manual "
        "the computers you train on are called <b>stations</b>. With a server:")
    s.bullets([
        f"{UI('Riders')} – a rider (profile) created on one station appears on all of them, with the same "
        "weight, FTP, picture and 3D figure. Change your FTP on one station and the others get it too.",
        f"{UI('Routes and videos')} – GPX routes and Real Life Videos are put on the server once. The stations "
        "fetch GPX routes by themselves; videos (large files) are fetched when you ask for them.",
        f"{UI('Training')} – every ride, row and run is sent to the server. Your History shows your rides from "
        "every station, and the ghost can race rides from the other stations and from other riders.",
        f"{UI('Analysis in the browser')} – the server has web pages with each rider's training: weekly hours, "
        "Training Load, FTP over time, best power and every ride with its chart.",
        f"{UI('Riding together')} – everyone on the same GPX route sees the others live in the 3D view.",
    ])
    s.note("The stations never wait for the server. If the server is switched off, every station keeps working "
           "on its own and sends what it has collected the next time the server is on. So the server only "
           "needs to be on while you train.")

    s.section("What you need")
    s.table(["You need", "Why", "Notes"], [
        ["One computer to be the server", "It keeps the riders, routes, videos and training for everyone.",
         "Any of: a Windows PC, a Mac, a computer with Debian or Ubuntu Linux, or a Raspberry Pi (4, 5 or "
         "500/500+). It can be a station at the same time, but a small computer that is always in the same "
         "place is easiest – a Raspberry Pi is ideal."],
        ["Your home network (router)", "The stations reach the server through it.",
         "All computers must be on the <b>same</b> network – the same router, by cable or Wi-Fi. A guest Wi-Fi "
         "usually does <b>not</b> work (see 9.1)."],
        ["Artilody 2.0 or newer on every station", "Older versions have no server settings.",
         f"Download from {L(RELEASES)}. The user manual explains how to install it."],
        [f"The server files: {C(ZIP)}", "The server's program.",
         f"On the same page as the app: {L(RELEASES)} (under <i>Assets</i>)."],
        ["About 30 minutes", "", "Most of it is waiting for downloads."],
        ["Disk space on the server", "Videos are large.",
         "A few hundred MB for the program and training; each video adds 1–10 GB."],
    ], [1.6, 1.8, 3.2])

    s.section("Which chapter is for you?")
    s.p("Pick the system of the computer that will be the <b>server</b>, and follow only that chapter. "
        "Then everyone continues with chapter 7 (the stations).")
    s.table(["The server computer runs …", "Method", "Chapter"], [
        ["Windows 10 or 11", "XAMPP (a ready-made web server package)", "3"],
        ["macOS (any Mac)", "XAMPP", "4"],
        ["Raspberry Pi OS (Raspberry Pi 4, 5, 500, 500+)", "The installer script install.sh", "5"],
        ["Debian or Ubuntu Linux", "The installer script install.sh", "6"],
    ], [2.6, 2.8, 0.8])
    s.tip("You may have heard of <b>LAMP</b>, <b>LAMPP</b> or <b>XAMPP</b>. They all mean the same kind of "
          "thing: a web server (Apache), a database (MySQL/MariaDB) and the programming language PHP. On Windows "
          "and Mac, XAMPP installs all three in one go. On Linux and Raspberry Pi our script installs them for "
          "you, so you do not need XAMPP there.")

    s.section("Words used in this manual")
    s.table(["Word", "What it means"], [
        ["Server", "The one computer that keeps everything for everyone."],
        ["Station", "A computer you train on, with the Artilody app."],
        ["Router", "The box from your internet provider that makes your home network (Wi-Fi). It has a sticker "
                   "with its name and password."],
        ["IP address", "A computer's number on your home network, like <b>192.168.1.20</b>. The stations use the "
                       "server's IP address to find it. It always looks like four numbers with dots."],
        ["Browser", "The program you use for web pages: Chrome, Edge, Safari or Firefox."],
        ["Address bar", "The long field at the top of the browser where web addresses are typed."],
        ["Terminal", "A window where you type commands (Linux, Raspberry Pi and Mac). Chapter 2.4 shows how to "
                     "open it."],
        ["Folder", "Called a <i>directory</i> in some programs."],
        ["Admin password", "A password you choose for the server's web pages, to manage riders, media and the "
                           "server."],
    ], [1.4, 5.2])

    # =====================================================================
    s.chapter("Basics for every server", lead=
              "Three things are the same whichever system you use: the server needs a fixed address on your "
              "network, it must not fall asleep while you train, and you need to know how to find its address.")

    s.section("Find the server's IP address")
    s.p("You will need the server's IP address several times. Write it down when you have found it.")
    s.sub("On Windows")
    s.steps([
        f"Click the {B('Start')} button (the Windows logo at the bottom of the screen).",
        f"Type {C('cmd')} and press {K('Enter')}. A black window opens (the <i>Command Prompt</i>).",
        f"Type {C('ipconfig')} and press {K('Enter')}.",
        f"Look for the paragraph {C('Wireless LAN adapter Wi-Fi')} (Wi-Fi) or {C('Ethernet adapter Ethernet')} "
        f"(cable). The line {C('IPv4 Address')} shows the address, for example {C('192.168.1.20')}.",
        "Close the black window.",
    ])
    s.sub("On a Mac")
    s.steps([
        f"Click the Apple menu (the apple at the top left) in the top-left corner and choose {B('System Settings')}.",
        f"Click {B('Wi-Fi')} (or {B('Network')} and then the cable connection, if the Mac uses a cable).",
        f"Click {B('Details…')} next to the network you are connected to.",
        f"Click {B('TCP/IP')}. The line {B('IP address')} shows the address, for example {C('192.168.1.20')}.",
    ])
    s.sub("On Linux and Raspberry Pi")
    s.p(f"Open a terminal (2.4), type {C('hostname -I')} and press {K('Enter')}. The first number shown is the "
        f"address, for example {C('192.168.1.20')}. (The installer in chapters 5 and 6 also shows it at the end.)")
    s.note(f"Addresses that start with {C('127.')} or {C('169.254.')} are not the right ones: {C('127.0.0.1')} "
           "always means \"this computer itself\", and 169.254… means the computer has no proper network "
           "connection.")

    s.section("Give the server a fixed address")
    s.p("Normally your router hands out addresses as computers come and go, and the server could get a "
        "different address next week – then the stations would no longer find it. The safest fix is to tell "
        "the router always to give the server the same address. This is called a <b>DHCP reservation</b> "
        "(routers also call it <i>static lease</i>, <i>address reservation</i>, <i>fixed IP</i> or "
        "<i>static DHCP</i>).")
    s.steps([
        "Find your router's address. It is usually printed on the sticker on the router, often as "
        f"{C('192.168.1.1')}, {C('192.168.0.1')}, {C('192.168.8.1')} or {C('10.0.0.1')}. On Windows, "
        f"{C('ipconfig')} (2.1) shows it as {C('Default Gateway')}; on a Mac it is {B('Router')} under TCP/IP.",
        "Type that address in the browser's address bar on any computer at home and press "
        f"{K('Enter')}. The router's own page opens.",
        "Log in. The router's admin password is usually on the same sticker (it is often <b>not</b> the Wi-Fi "
        "password).",
        f"Look for a menu called {B('LAN')}, {B('DHCP')}, {B('Network')}, {B('Local network')} or "
        f"{B('Clients')}. In it, find {B('Address reservation')} / {B('Static lease')} / {B('Fixed IP')}.",
        "Pick the server in the list of connected devices (by its name, or by the address you found in 2.1) "
        f"and choose {B('Reserve')} / {B('Add')} / {B('Always use this address')}. Keep the address it has now.",
        f"Click {B('Save')} / {B('Apply')}.",
        "Restart the server computer once, and check its address again (2.1): it must be the reserved one.",
    ])
    s.tip("Every router looks different. If you cannot find the setting, search the internet for your router's "
          "model name (on the sticker) together with the words \"DHCP reservation\". Your internet provider's "
          "help pages usually explain it for their router.")
    s.warning("Do <b>not</b> set up \"port forwarding\" for the server in the router. The server is meant for "
              "your home network only and must not be reachable from the internet.")

    s.section("Keep the server awake while you train")
    s.p("A server that goes to sleep cannot answer. Set it so it does not sleep (the screen may still turn off).")
    s.bullets([
        f"<b>Windows 11:</b> {B('Start')} › {B('Settings')} › {B('System')} › {B('Power &amp; battery')} › "
        f"{B('Screen and sleep')} › set <i>When plugged in, put my device to sleep after</i> to {B('Never')}.",
        f"<b>Windows 10:</b> {B('Start')} › {B('Settings')} › {B('System')} › {B('Power &amp; sleep')} › "
        f"under <i>Sleep</i> choose {B('Never')}.",
        f"<b>Mac:</b> Apple menu (the apple at the top left) › {B('System Settings')} › {B('Energy')} (a desktop Mac) or {B('Battery')} › "
        f"{B('Options…')} (a laptop) › switch on {B('Prevent automatic sleeping when the display is off')}.",
        "<b>Raspberry Pi, Debian, Ubuntu without a screen (server use):</b> nothing to do – they do not sleep.",
        f"<b>Ubuntu / Debian desktop:</b> {B('Settings')} › {B('Power')} › set {B('Automatic Suspend')} to "
        f"{B('Off')}.",
    ])

    s.section("Open a terminal (Linux, Raspberry Pi, Mac)")
    s.p("A terminal is a window where you type a command and press Enter. Type the commands exactly as they are "
        "printed in this manual – spaces and capital letters matter. You can also copy them from the PDF and "
        "paste them.")
    s.bullets([
        f"<b>Raspberry Pi OS with a desktop:</b> click the black screen icon at the top of the screen "
        f"({B('Terminal')}), or press {K('Ctrl')}+{K('Alt')}+{K('T')}.",
        f"<b>Ubuntu:</b> press {K('Ctrl')}+{K('Alt')}+{K('T')}, or click {B('Show Apps')} (the dots at the bottom "
        f"left) and choose {B('Terminal')}.",
        f"<b>Debian:</b> click {B('Activities')} (top left), type <i>terminal</i> and press {K('Enter')}.",
        f"<b>Mac:</b> press {K('Cmd')}+{K('Space')}, type <i>Terminal</i> and press {K('Enter')}.",
    ])
    s.p(f"When a command starts with {C('sudo')}, the computer asks for <b>your</b> password (the one you log in "
        "with). Nothing appears while you type it – not even dots. That is normal: type it and press "
        f"{K('Enter')}.")
    s.tip(f"Paste in a terminal with {K('Ctrl')}+{K('Shift')}+{K('V')} on Linux and Raspberry Pi (plain "
          f"{K('Ctrl')}+{K('V')} does not work there), and with {K('Cmd')}+{K('V')} on a Mac.")

    # =====================================================================
    s.chapter("Windows server (XAMPP)", lead=
              "On Windows the server runs in XAMPP: a free, ready-made package with the Apache web server, the "
              "MariaDB database and PHP. You install XAMPP, copy the Artilody server folder into it and finish "
              "the setup in your browser.")

    s.section("Install XAMPP")
    s.steps([
        f"Open the browser and go to {L('https://www.apachefriends.org')}.",
        f"Click {B('XAMPP for Windows')}. The download starts (a file called "
        f"{C('xampp-windows-x64-8.2.…-installer.exe')}, about 150 MB). Choose the version with <b>PHP 8.2</b> "
        "(or newer) if you are asked.",
        f"When it has downloaded, open it (click it at the bottom/top of the browser, or double-click it in the "
        f"{B('Downloads')} folder).",
        f"Windows asks <i>Do you want to allow this app to make changes to your device?</i> – click {B('Yes')}.",
        f"A message about <b>User Account Control (UAC)</b> may appear and say not to install in "
        f"{WP0}. Click {B('OK')} – we install in {WP1}, which is fine.",
        f"In the setup window click {B('Next')}.",
        f"<b>Select Components:</b> keep {B('Apache')}, {B('MySQL')}, {B('PHP')} and {B('phpMyAdmin')} ticked. "
        f"The others (FileZilla, Mercury, Tomcat, Perl, Webalizer, Fake Sendmail) are not needed – you may untick "
        f"them. Click {B('Next')}.",
        f"<b>Installation folder:</b> keep {WP1}. Click {B('Next')}.",
        f"<b>Language:</b> choose English (or German) and click {B('Next')}. Untick <i>Learn more about Bitnami</i> "
        f"and click {B('Next')} twice. The installation takes a few minutes.",
        f"At the end keep <i>Do you want to start the Control Panel now?</i> ticked and click {B('Finish')}. "
        f"(If it asks for a language, choose English and click {B('Save')}.)",
    ])

    s.section("Start Apache and MySQL")
    s.p(f"The <b>XAMPP Control Panel</b> has a row for each part. You need two of them: <b>Apache</b> (the web "
        "server) and <b>MySQL</b> (the database).")
    s.steps([
        f"In the row <b>Apache</b>, click {B('Start')}. After a moment the name turns green.",
        f"Windows may show <i>Windows Defender Firewall has blocked some features of this app</i> for "
        f"<b>Apache HTTP Server</b>. Tick {B('Private networks')}, untick <i>Public networks</i>, and click "
        f"{B('Allow access')}. <b>This is important</b> – without it the stations cannot reach the server.",
        f"In the row <b>MySQL</b>, click {B('Start')}. The name turns green.",
        f"If the firewall asks about <b>mysqld</b>, click {B('Cancel')} – the database is only used by the server "
        "itself and must not be reachable from the network.",
        f"Check: open the browser and go to {C('http://localhost')}. A XAMPP welcome page appears. "
        "(If not, see 9.3.)",
    ])
    s.tip(f"To start XAMPP by itself when Windows starts: in the Control Panel click {B('Config')} (top right), "
          f"tick {B('Apache')} and {B('MySQL')} under <i>Autostart of modules</i>, and click {B('Save')}. Then "
          f"put a shortcut to {WP5} in the Startup folder: press {K('Win')}+{K('R')}, "
          f"type {C('shell:startup')}, press {K('Enter')}, and copy the shortcut into the folder that opens.")

    s.section("Copy the Artilody server into XAMPP")
    s.steps([
        f"Go to {L(RELEASES)}. Under <b>Assets</b>, click {C(ZIP)} to download it.",
        f"Open the {B('Downloads')} folder in File Explorer. Right-click {C(ZIP)} and choose "
        f"{B('Extract All…')}, then {B('Extract')}. A new folder opens; inside it is a folder called "
        f"{C('artilody')}.",
        f"Open a second File Explorer window (press {K('Win')}+{K('E')}) and go to {WP3} "
        f"(click {B('This PC')}, then {B('Local Disk (C:)')}, then {C('xampp')}, then {C('htdocs')}).",
        f"Drag the folder {C('artilody')} into {C('htdocs')} (or right-click it, {B('Copy')}, and in "
        f"{C('htdocs')} right-click, {B('Paste')}).",
        f"Check: open {WP4}. You must see the folders {C('public')}, {C('lib')} and "
        f"{C('data')} and the file {C('schema.sql')} directly inside it. If you see another {C('artilody')} "
        "folder instead, move that inner one up so the files are directly inside "
        f"{WP4}.",
    ])

    s.section("Finish the setup in the browser")
    s.p("The setup page creates the database, checks that everything is in place and asks for the admin password "
        "of the web interface.")
    s.steps([
        f"On the <b>server computer itself</b>, open the browser and go to "
        f"{C('http://localhost/artilody/setup.php')}.",
        "Part <b>1. Check</b> shows a list. All lines must have a green tick. If a line is red, it says what to "
        "do (see also 9.4), then reload the page.",
        f"Part <b>2. Database</b>: keep {B('root')} as database administrator and leave its password "
        f"<b>empty</b> (that is how XAMPP comes). Keep {C('127.0.0.1')} and {C('3306')}.",
        f"Part <b>3. The server</b>: type a name (for example <i>Artilody server</i>) and choose an "
        f"<b>admin password</b> of at least 8 characters, twice. <b>Write the admin password down.</b>",
        f"Click {B('Set up the server')}.",
        f"The page says <i>The server is set up</i> and shows the address for the stations, for example "
        f"{C('192.168.1.20/artilody')}. <b>Write it down</b> – you type it on every station (chapter 7).",
    ])
    s.figure(NET / "web-setup.png", "The setup page (setup.php) on a new server: every check is green; the "
             "database part keeps XAMPP's user root with an empty password.", width_frac=0.85)
    s.p("Continue with section 3.5, then chapter 7.")

    s.section("Make the address fixed and the PC awake")
    s.bullets([
        "Give the server a fixed address in the router (2.2).",
        "Set Windows not to sleep (2.3).",
        "Check from another computer: type the address from the setup page with http:// in front, for example "
        f"{C('http://192.168.1.20/artilody')}, in that computer's browser. The server's status page must appear. "
        "If it does not, see 9.2.",
    ])
    s.warning("The address for XAMPP always ends in <b>/artilody</b>. Without it the stations reach the XAMPP "
              "welcome page instead of the Artilody server and say \"HTTP 404 – not an Artilody server\".")

    # =====================================================================
    s.chapter("Mac server (XAMPP)", lead=
              "On a Mac the server also runs in XAMPP. The steps are the same as on Windows; only the windows look "
              "different, and the Artilody folder must be made writable for the web server.")

    s.section("Install XAMPP")
    s.steps([
        f"Open the browser and go to {L('https://www.apachefriends.org')}.",
        f"Click {B('XAMPP for OS X')} and download the <b>installer</b> (a file called "
        f"{C('xampp-osx-8.2.…-installer.dmg')}). Choose the version with PHP 8.2 if you are asked. "
        "(Do not choose <i>XAMPP-VM</i>.)",
        f"Open the downloaded file in the {B('Downloads')} folder. A window opens with the XAMPP icon; "
        "double-click it.",
        f"If the Mac says it <i>cannot be opened because Apple cannot check it</i>: click {B('Done')} / "
        f"{B('OK')}, open Apple menu (the apple at the top left) › {B('System Settings')} › {B('Privacy &amp; Security')}, scroll down to "
        f"the message about XAMPP and click {B('Open Anyway')}. Type your Mac password if asked.",
        f"Type your Mac password when the installer asks. Click {B('Next')} through the setup, keeping "
        f"{B('XAMPP Core Files')} ticked. Untick <i>Learn more about Bitnami</i>. Click {B('Next')} until it "
        f"installs, then {B('Finish')}.",
        f"Apple-silicon Macs (M1 and newer) may ask to install <b>Rosetta</b> first – click {B('Install')}.",
    ])

    s.section("Start Apache and MySQL")
    s.steps([
        f"Open the {B('Applications')} folder, then the folder {C('XAMPP')}, and double-click {B('manager-osx')} "
        f"(it is called <i>XAMPP</i> when it opens).",
        f"Click the tab {B('Manage Servers')}.",
        f"Select {B('MySQL Database')} and click {B('Start')}. Wait until it says <i>Running</i>.",
        f"Select {B('Apache Web Server')} and click {B('Start')}. Wait until it says <i>Running</i>.",
        f"If the Mac asks <i>Do you want the application \"httpd\" to accept incoming network connections?</i>, "
        f"click {B('Allow')}.",
        f"Check: open the browser and go to {C('http://localhost')}. A XAMPP welcome page appears.",
    ])

    s.section("Copy the Artilody server into XAMPP")
    s.steps([
        f"Go to {L(RELEASES)} and download {C(ZIP)} under <b>Assets</b>.",
        f"In the {B('Downloads')} folder, double-click {C(ZIP)}. A folder called {C('artilody')} appears.",
        f"In Finder choose {B('Go')} › {B('Go to Folder…')} (top menu bar), type "
        f"{C('/Applications/XAMPP/xamppfiles/htdocs')} and press {K('Enter')}.",
        f"Drag the folder {C('artilody')} from {B('Downloads')} into this {C('htdocs')} window. Type your Mac "
        "password if asked.",
        f"Check: open {C('htdocs/artilody')}. You must see the folders {C('public')}, {C('lib')} and {C('data')} "
        f"and the file {C('schema.sql')} directly inside it.",
    ])

    s.section("Let the web server write in the folder")
    s.p("XAMPP's web server runs as a separate user and must be allowed to write in the Artilody folder "
        "(its configuration and the media files are kept there).")
    s.steps([
        f"In the {C('htdocs')} window, click the folder {C('artilody')} once to select it.",
        f"Choose {B('File')} › {B('Get Info')} (or press {K('Cmd')}+{K('I')}).",
        f"At the bottom, open {B('Sharing &amp; Permissions')} (click the small arrow if it is closed).",
        "Click the lock at the bottom right and type your Mac password.",
        f"In the row <b>everyone</b>, click the right column and choose {B('Read &amp; Write')}.",
        f"Click the round button with three dots (…) at the bottom and choose {B('Apply to enclosed items…')}, "
        f"then {B('OK')}.",
        "Close the Info window.",
    ])

    s.section("Finish the setup in the browser")
    s.p(f"Exactly as on Windows (3.4): on the Mac itself, open {C('http://localhost/artilody/setup.php')}, check "
        f"that every line is green, keep the database user {B('root')} with an <b>empty</b> password, choose and "
        f"write down an admin password, click {B('Set up the server')}, and write down the address for the "
        f"stations (for example {C('192.168.1.20/artilody')}).")
    s.bullets([
        "Give the Mac a fixed address in the router (2.2).",
        "Set the Mac not to sleep (2.3).",
        f"After a restart of the Mac, open XAMPP ({B('manager-osx')}) and start MySQL and Apache again before "
        "training – make it part of your routine, like switching on the trainer.",
    ])
    s.note("If the Mac's firewall is on (System Settings › Network › Firewall) and the stations cannot connect, "
           f"open {B('Options…')} there and set <b>httpd</b> to {B('Allow incoming connections')}.")

    # =====================================================================
    s.chapter("Raspberry Pi server", lead=
              "A Raspberry Pi is a small, quiet and cheap computer – ideal as a server that sits in a corner and is "
              "switched on when you train. Our installer script sets up everything (web server, database, PHP, "
              "backups) in one go.")

    s.section("What you need")
    s.bullets([
        "A Raspberry Pi 4, 5, 500 or 500+ (2 GB of memory or more) with its power supply.",
        "A microSD card of 32 GB or more (or a USB SSD for many videos), and a computer with a card reader to "
        "prepare it.",
        "Preferably a network cable from the Pi to the router (Wi-Fi works, but videos come faster by cable).",
        "For the first start: a screen, keyboard and mouse – or another computer (see 5.3).",
    ])

    s.section("Prepare the memory card")
    s.p("If your Pi already runs Raspberry Pi OS, skip to 5.3.")
    s.steps([
        f"On another computer, download <b>Raspberry Pi Imager</b> from {L('https://www.raspberrypi.com/software')} "
        "and install it.",
        "Put the memory card in the card reader and open Raspberry Pi Imager.",
        f"{B('Choose Device')}: your Raspberry Pi model. {B('Choose OS')}: <b>Raspberry Pi OS (64-bit)</b>. "
        f"{B('Choose Storage')}: the memory card.",
        f"Click {B('Next')}, then {B('Edit Settings')}. Fill in:<br/>"
        f"– <b>hostname</b>: for example {C('artilody-server')}<br/>"
        "– <b>username</b> and <b>password</b>: choose them and write them down<br/>"
        "– <b>Wireless LAN</b>: your Wi-Fi name and password (skip if you use a cable)<br/>"
        "– <b>Locale</b>: your time zone and keyboard<br/>"
        f"– tab {B('Services')}: tick {B('Enable SSH')} with password authentication (lets you work on the Pi "
        "from another computer).",
        f"Click {B('Save')}, then {B('Yes')} to use the settings, and {B('Yes')} to write the card. Wait until it "
        "is done, then put the card in the Pi and plug in the power.",
    ])

    s.section("Get a terminal on the Pi")
    s.p("Either directly on the Pi, or from another computer.")
    s.bullets([
        "<b>With a screen, keyboard and mouse on the Pi:</b> log in and open a terminal (2.4).",
        f"<b>From another computer (SSH):</b> find the Pi's address in your router's list of devices (2.2) or try "
        f"its name. On a Mac open Terminal; on Windows 10/11 click {B('Start')}, type {C('cmd')} and press "
        f"{K('Enter')}. Then type (with your username and the Pi's address):",
    ])
    s.code("ssh lone@192.168.1.20")
    s.p(f"The first time it asks <i>Are you sure you want to continue connecting?</i> – type {C('yes')} and press "
        f"{K('Enter')}. Then type the Pi's password (nothing appears while you type). You now have a terminal on "
        f"the Pi; {C('exit')} closes it.")
    s.tip(f"If the name works, you can use {C('ssh lone@artilody-server.local')} instead of the number.")

    s.section("Install the Artilody server")
    s.p("Type these commands in the Pi's terminal, one at a time, each followed by Enter. The first one asks for "
        "your password.")
    s.code("sudo apt update\nsudo apt install -y unzip wget")
    s.p(f"Now download the server. The easiest way is to copy the link: on {L(RELEASES)}, right-click "
        f"{C(ZIP)} under <b>Assets</b> and choose <i>Copy link address</i>. Then type {C('wget')}, a space, paste "
        "the link and press Enter. It looks like this:")
    s.code(f"wget {REPO}/releases/download/v{V}/{ZIP}")
    s.p("Unpack it and start the installer:")
    s.code(f"unzip {ZIP}\ncd artilody\nsudo bash install.sh")
    s.p("The installer works for a few minutes and prints its steps (lines starting with ==>). It installs the "
        "web server, the database and PHP if they are missing, creates the database with its own password, "
        "sets up backups and checks that everything works. At the end:")
    s.steps([
        "It asks for the <b>admin password</b> of the web interface: type one of at least 8 characters, press "
        "Enter, type it again and press Enter. (Nothing appears while you type.) <b>Write it down.</b>",
        f"It prints <i>OK – http://192.168.1.20/</i> and <i>The stations: … address: 192.168.1.20</i>. That "
        f"number is the address you type on the stations. <b>Write it down.</b>",
    ])
    s.figure(NET / "web-status.png", "The server's status page (here with three stations). Open it from any "
             "computer at home: http:// followed by the server's address.", width_frac=0.85)
    s.p("Finally give the Pi a fixed address in the router (2.2) and restart it once:")
    s.code("sudo reboot")
    s.p("Continue with chapter 7.")
    s.note("The installer replaces the Pi's default web page with the Artilody server. If the Pi already serves "
           "another website, use a separate Pi or ask someone used to web servers. The database is reachable only "
           "on the Pi itself, never from the network.")

    # =====================================================================
    s.chapter("Debian and Ubuntu server", lead=
              "On a computer with Debian or Ubuntu Linux, the same installer script as on the Raspberry Pi does "
              "everything. A Debian 12 or 13, or Ubuntu 22.04 or newer, desktop or server edition, will do.")

    s.section("Make sure you may use sudo")
    s.p(f"The installer needs {C('sudo')}. Open a terminal (2.4) and type:")
    s.code("sudo whoami")
    s.p(f"If it answers {C('root')} after your password, continue with 6.2. If it says your user is <i>not in the "
        "sudoers file</i> (this happens on Debian when a root password was set during installation), do this "
        "once:")
    s.code("su -\nusermod -aG sudo YOURNAME\nexit")
    s.p(f"{C('su -')} asks for the <b>root</b> password (the one chosen when Debian was installed). Replace "
        f"{C('YOURNAME')} with your login name. Then log out and in again (or restart) and try "
        f"{C('sudo whoami')} once more.")

    s.section("Install the Artilody server")
    s.p("Exactly as on the Raspberry Pi (5.4). In the terminal, one line at a time:")
    s.code(f"sudo apt update\nsudo apt install -y unzip wget\n"
           f"wget {REPO}/releases/download/v{V}/{ZIP}\nunzip {ZIP}\ncd artilody\nsudo bash install.sh")
    s.p("Choose and write down the admin password when asked, and write down the address it prints at the end.")

    s.section("The firewall (Ubuntu)")
    s.p("Debian has no firewall switched on by default. Ubuntu has one (ufw) that is usually off. To check and, if "
        "needed, open the web server for your home network:")
    s.code("sudo ufw status\nsudo ufw allow 80/tcp")
    s.p(f"If {C('sudo ufw status')} says <i>Status: inactive</i>, you need nothing more. Then: a fixed address in "
        "the router (2.2), no sleep (2.3), and continue with chapter 7.")

    s.section("XAMPP on Linux (only if you prefer)")
    s.p("XAMPP also exists for Linux (on ordinary PCs, not on the Raspberry Pi). If you already use it, copy the "
        f"{C('artilody')} folder to {C('/opt/lampp/htdocs/artilody')}, let the web server write in it "
        f"({C('sudo chown -R daemon /opt/lampp/htdocs/artilody')}), start XAMPP with "
        f"{C('sudo /opt/lampp/lampp start')} and finish in the browser as in 3.4. The station address then ends in "
        f"{C('/artilody')}. For a new server, the installer script (6.2) is simpler.")

    # =====================================================================
    s.chapter("Connect the stations", lead=
              "Do this on every computer you train on. You need the server's address from the setup page or the "
              "installer, for example 192.168.1.20 (Raspberry Pi, Debian, Ubuntu) or 192.168.1.20/artilody "
              "(XAMPP on Windows or Mac).")

    s.section("Switch the server on in the app")
    s.steps([
        "Make sure the station has Artilody 2.0 or newer (the version is shown in Settings) and is connected to "
        "the same network as the server.",
        f"Open Artilody and click {B('Settings')} (the gear in the sidebar).",
        f"Switch on {B('Server')}.",
        "Click in the address field and type the server's address – without http://, for example "
        f"{C('192.168.1.20')} or {C('192.168.1.20/artilody')}. Press {K('Enter')}.",
        f"Within a few seconds a green dot and <i>Connected to Artilody server</i> appear. If not, click "
        f"{B('Check now')}; if it stays red, see chapter 9.",
    ])
    s.figure(NET / "app-settings-server.png", "Settings › Server on a station: the address of a XAMPP server "
             "(here a test server with a port number; yours is like 192.168.1.20/artilody), the green "
             "connection and the state of profiles, training and media.", width_frac=0.62)
    s.p("Below the connection, three lines show what is being shared:")
    s.table(["Line", "What it means"], [
        [UI("Profiles"), "Up to date – or how many profile changes wait to be sent."],
        [UI("Training"), "Up to date – or how many rides wait to be sent. The first time, the station's whole "
                         "history is sent; that can take a minute."],
        [UI("Media"), "How many GPX routes and videos the server has, and how many of the videos are on this "
                      "station."],
    ], [1.2, 5.4])

    s.section("Mac stations: allow the local network")
    s.p("macOS 15 (Sequoia) and newer only lets programs talk to other computers at home with your permission. "
        "The first time Artilody connects, macOS asks <i>Allow Artilody … to find devices on local "
        f"networks?</i> – click {B('Allow')}.")
    s.p(f"If you clicked <i>Don't Allow</i>, or the dot stays red with <i>Host unreachable</i>: Apple menu (the apple at the top left) › "
        f"{B('System Settings')} › {B('Privacy &amp; Security')} › {B('Local Network')}, switch on "
        f"{B('Artilody Multisport Endurance Trainer')}, quit Artilody ({K('Cmd')}+{K('Q')}) and start it again. "
        "The same applies to your browser (Chrome, Firefox) if it cannot open the server's pages.")

    s.section("What happens next")
    s.bullets([
        f"{UI('Riders')} – the first station sends its riders to the server. A new station with only the starter "
        "profile takes the server's riders instead. From then on, a change on one station reaches the others "
        "within a minute.",
        f"{UI('GPX routes')} – the server's routes appear by themselves in the GPX Ride library.",
        f"{UI('Training')} – each finished ride is sent a few seconds after it is saved. Rides from other stations "
        "appear in History with <i>on &lt;station&gt;</i>.",
        f"{UI('During a ride')} – nothing is fetched or sent while you ride, so the picture stays smooth; it "
        "happens afterwards.",
    ])
    s.note("A ride that belongs to a rider this station does not know yet is fetched as soon as that rider "
           "arrives. A rider deleted on the server disappears from the stations – except on a station that has "
           "rides by that rider.")

    # =====================================================================
    s.chapter("Using the server", lead=
              "Day to day you hardly notice the server. This chapter shows its web pages, the media library and "
              "riding together.")

    s.section("The web pages")
    s.p(f"Open the server's address with http:// in any browser at home: {C('http://192.168.1.20')} (Raspberry Pi, "
        f"Debian, Ubuntu) or {C('http://192.168.1.20/artilody')} (XAMPP). The menu at the top has:")
    s.table(["Page", "Login", "What it shows"], [
        [UI("Status"), "no", "Is the server running, and which stations have been in touch (green: just now)."],
        [UI("Training"), "no", "Every rider's training: the last 7/28/365 days, hours and Training Load per week, "
                               "FTP over time, best power, and the list of rides. Click a ride for its chart."],
        [UI("Users"), "admin", "Create, change and delete riders: name, weight, FTP, heart rates, picture and the "
                               "colours of the 3D figures."],
        [UI("Media"), "admin", "Upload GPX routes and videos, rename and delete them."],
        [UI("Server"), "admin", "Shut down or restart the server (Raspberry Pi, Debian, Ubuntu)."],
    ], [1.1, 0.8, 4.7])
    s.figure(NET / "web-training.png", "Training: one rider's totals, weekly hours and Training Load, FTP over "
             "time, best power and rides.", width_frac=0.8)
    s.figure(NET / "web-ride.png", "One ride: its numbers and the chart; tick the boxes above it to show heart "
             "rate, cadence or speed.", width_frac=0.8)
    s.figure(NET / "web-users.png", "Users (after logging in with the admin password).", width_frac=0.8)

    s.section("Upload routes and videos")
    s.steps([
        f"On the web pages click {B('Media')} and log in with the admin password.",
        f"Choose {B('GPX route')} or {B('Video route')}.",
        "Optionally type a name. Click the file field and choose the file(s): one .gpx for a GPX route; for a "
        "video route the video (mp4, m4v, mov, mkv, avi or webm) <b>and</b> its .gpx (and its .xml if it has one) "
        f"– hold {K('Ctrl')} (Windows) or {K('Cmd')} (Mac) to select several.",
        f"Click {B('Upload')}. A bar shows the progress; large videos take a while. Keep the page open until it "
        "says the file was uploaded.",
    ])
    s.figure(NET / "web-media.png", "Media: upload and the routes and videos on the server.", width_frac=0.8)
    s.p("You can also send a route that is already on a station: in GPX Ride or Video Ride, choose the route, "
        f"click the folder button and choose {B('Send to Server')}.")

    s.section("Fetch videos on a station")
    s.steps([
        f"Open {B('Video Ride')} (or {B('GPX Ride')} for routes), click the folder button next to the route list "
        f"and choose {B('Server Library…')}.",
        f"Click {B('Download')} next to the video. The progress is shown; you may close the window meanwhile.",
        "When it is done, the video is in the route list like any other. An interrupted download continues where "
        "it stopped the next time.",
    ])
    s.figure(NET / "app-server-videos.png", "Server Library on a station: Download fetches a video; routes "
             "already on the station say so.", width_frac=0.6)
    s.note("A GPX route edited on a station (GPX Workshop) is never overwritten by the server's version. The "
           f"Server Library marks it, and {B('Download copy')} fetches the server's as \"name (server).gpx\".")

    s.section("Ride together")
    s.p("When two or more stations are on the same GPX route while the server is on, they see "
        "each other: each other rider appears in the 3D view in its own lane, in the figure and colours of its own "
        "profile, and the panel "
        "<b>Riding with</b> shows how far ahead (+) or behind (−) each one is, and their watts. Nothing needs to "
        "be switched on – just choose the same route. A rider is shown as soon as their GPX Ride page is open with "
        "the route, waiting at the start (\"ready\"), so you see each other before you set off. Start whenever you "
        "like; everyone is shown where they really are on the route.")
    s.p("Riders and runners mix: a runner on the treadmill (Treadmill Run) and a cyclist on the trainer "
        "(GPX Ride) who choose the same GPX file see each other – the cyclist on a bike, the runner running. "
        "Best on a short loop (start and finish at the same place, Loop route on): the cyclist laps the runner "
        "again and again. On a loop the panel also shows whole laps ahead or behind (\"+1 lap\"), and a bike "
        "or running icon by each name. A triangle in each rider's jersey colour marks where they are on the elevation "
        "profile, and with two or more others on the route a small name sign floats above each one's head.")
    s.p("The ghost list (GPX Ride and Treadmill Run) also offers the other riders' earlier rides on the route, "
        "marked with their name.")

    s.section("Shut the server down")
    s.bullets([
        f"<b>Raspberry Pi, Debian, Ubuntu:</b> web pages › {B('Server')} › {B('Shut down')}. Wait until the Pi's "
        "green light stops blinking before you pull the plug. The database is backed up on the way. Switch it on "
        "again simply by plugging it in (or pressing its power button).",
        f"<b>Windows and Mac (XAMPP):</b> in the XAMPP Control Panel / manager click {B('Stop')} for Apache and "
        "MySQL, then shut the computer down as usual.",
    ])
    s.warning("Do not pull the plug of a running server – the database may be damaged. Always shut it down first.")

    s.section("Backups")
    s.bullets([
        f"<b>Raspberry Pi, Debian, Ubuntu:</b> the database (riders, training, the media list) is backed up "
        f"automatically every time the server starts and shuts down; the newest 30 copies are kept in "
        f"{C('/var/backups/artilody')}. The media files themselves are in {C('/var/lib/artilody/blobs')} – copy "
        "that folder to a USB disk now and then if the videos matter to you.",
        f"<b>XAMPP:</b> back up the database with phpMyAdmin: on the server open {C('http://localhost/phpmyadmin')}, "
        f"click {B('artilody')} on the left, then {B('Export')} › {B('Export')}. Keep the file. The media files "
        f"are in {C('htdocs/artilody/public/blobs')}.",
        "The stations keep their own copy of everything they have, so the server is never the only copy of your "
        "training.",
    ])

    s.section("Update the server")
    s.p("When a new Artilody release comes out, update the server too (the stations and the server should have "
        "the same version).")
    s.bullets([
        f"<b>Raspberry Pi, Debian, Ubuntu:</b> download and unpack the new zip as in 5.4 (first remove the old "
        f"folder: {C('rm -rf artilody')}), then {C('cd artilody')} and {C('sudo bash install.sh')}. Your data "
        "and passwords are kept.",
        f"<b>XAMPP:</b> stop Apache, copy the new {C('artilody')} folder over the old one in {C('htdocs')} and "
        f"choose to replace the files – your {C('config.php')} and the media files are not in the zip, so they "
        f"stay. Start Apache, open {C('http://localhost/artilody/setup.php')} and click "
        f"{B('Update the database')}.",
    ])

    # =====================================================================
    s.chapter("When something does not work", lead=
              "Start at the top: most problems are the network, the address or a firewall.")

    s.section("The station says \"Not connected\"")
    s.table(["The message says …", "What to do"], [
        ["<i>Host unreachable</i> / <i>Connection refused</i> / <i>no answer within 5 s</i>",
         "1. Is the server switched on, and are Apache and MySQL started (XAMPP)? 2. Does the address in Settings "
         "match the server's address (2.1)? 3. Are the station and the server on the same network – not a guest "
         "Wi-Fi? 4. Mac station: allow the local network (7.2). 5. Windows server: allow Apache in the firewall "
         "(9.2)."],
        ["<i>HTTP 404 – not an Artilody server</i>",
         "The address reaches a web server, but not the Artilody server. XAMPP: the address must end in "
         "/artilody. Check that the files are directly in htdocs\\artilody (3.3)."],
        ["<i>HTTP 500 – the server has a problem</i>",
         "Open the server's address in a browser. XAMPP: open setup.php on the server and fix red lines. "
         "Raspberry Pi / Linux: run sudo bash install.sh again."],
        ["<i>the server's database does not answer</i> (yellow)",
         "XAMPP: start MySQL in the Control Panel. Raspberry Pi / Linux: restart the server."],
        ["<i>Not a valid address</i>", "Type only the address, e.g. 192.168.1.20 or 192.168.1.20/artilody – no "
                                       "spaces."],
    ], [2.2, 4.4])

    s.section("Other computers cannot open the server")
    s.bullets([
        "On the server itself, http://localhost/artilody (XAMPP) or http://localhost (Raspberry Pi / Linux) must "
        "work. If not, Apache is not running.",
        f"<b>Windows server:</b> the firewall may block Apache. Click {B('Start')}, type <i>Allow an app through "
        f"Windows Firewall</i> and open it, click {B('Change settings')}, find <b>Apache HTTP Server</b> and tick "
        f"{B('Private')}. Click {B('OK')}. If Apache is not in the list, click {B('Allow another app…')} › "
        f"{B('Browse…')} and choose {WP2}.",
        "Your network may be marked <i>Public</i> on the Windows server, which blocks visitors. Click the network "
        f"icon › {B('Properties')} of your Wi-Fi/network and choose {B('Private network')}.",
        "<b>Mac server:</b> see the note at the end of 4.5.",
        "<b>Mac browser:</b> allow the browser in Privacy &amp; Security › Local Network (7.2).",
        "Some routers keep Wi-Fi devices apart (<i>client isolation</i>, <i>AP isolation</i>). Switch it off in "
        "the router, or connect the server with a cable.",
    ])

    s.section("XAMPP: Apache or MySQL will not start")
    s.bullets([
        "<b>Apache: port 80 in use.</b> Another program uses the web port – often Skype (older versions), IIS or "
        f"<i>World Wide Web Publishing Service</i>. In the Control Panel click {B('Netstat')} to see which. Quit "
        "that program (or switch off the service in Windows' <i>Services</i>) and start Apache again.",
        "<b>MySQL: port 3306 in use.</b> Another database (MySQL, MariaDB) is installed. Stop it in Windows' "
        "<i>Services</i>, or uninstall it if you do not need it.",
        f"<b>MySQL shut down unexpectedly.</b> Click {B('Logs')} next to MySQL for the reason. After a power cut, "
        "a restart of the computer often helps.",
        "<b>Mac:</b> if Apache will not start, another web server may run. Restart the Mac and start XAMPP again.",
    ])

    s.section("The setup page shows red lines")
    s.table(["Red line", "What to do"], [
        ["PDO MySQL / mbstring / fileinfo",
         f"XAMPP Control Panel › Apache {B('Config')} › {B('PHP (php.ini)')}. In the file, find the line (e.g. "
         f"{C(';extension=fileinfo')}), remove the ; at its start, save, and restart Apache."],
        ["Uploads of 9 MB / Memory", f"In the same php.ini set {C('post_max_size = 16M')}, "
                                     f"{C('upload_max_filesize = 16M')} and {C('memory_limit = 256M')}, save, "
                                     "restart Apache."],
        ["… can be written", "Mac: make the folder writable (4.4). Windows: copy the folder to C:\\xampp\\htdocs, "
                             "not to Documents or the Desktop."],
        ["PHP 8.1 or newer", "Install a newer XAMPP (PHP 8.2)."],
        ["mod_rewrite", "In the Control Panel › Apache Config › httpd.conf, find "
                        f"{C('#LoadModule rewrite_module')} and remove the #. Save and restart Apache."],
    ], [1.8, 4.8])
    s.p("The setup page opens only on the server computer itself (for safety). Use "
        f"{C('http://localhost/artilody/setup.php')} there, not the IP address.")

    s.section("I forgot the admin password")
    s.bullets([
        f"<b>Raspberry Pi, Debian, Ubuntu:</b> in a terminal on the server: {C('sudo artilody-admin-password')}.",
        f"<b>XAMPP:</b> delete {C('config.php')} in {C('htdocs/artilody')} and open setup.php again: it sets up "
        "the server once more with a new admin password. The riders, training and media are kept.",
    ])

    s.section("Uploads of large videos stop")
    s.bullets([
        "Keep the browser tab open and the computer awake until the upload is finished.",
        "Check that the server has enough free disk space (shown on the Media page).",
        "Uploading from a station (Send to Server) continues even if you close the dialog, and goes on when you "
        "try again.",
    ])

    s.section("Getting help")
    s.p(f"Describe what you did, what you expected and what you see (a photo of the screen helps) in an issue: "
        f"{L(REPO + '/issues')}. On a station, Settings › Diagnostic log › {B('Export logs…')} gives a file that "
        "shows the server connection – attach it.")

    # =====================================================================
    s.chapter("Safety", appendix=True)
    s.bullets([
        "The server is for your home or club network only. Never forward ports to it in the router and never put "
        "it on the internet.",
        "Only the admin pages (users, media, shutting down) need the password. The status and training pages can "
        "be seen by everyone on your network – choose the network accordingly.",
        "The stations' connection to the server is not encrypted (http). On a home network that is normal; do "
        "not use the server over public Wi-Fi.",
        "XAMPP: phpMyAdmin only opens on the server computer itself (XAMPP's default). Do not change that. "
        "Keep MySQL blocked in the firewall (3.2).",
        "Raspberry Pi / Linux: the database only listens on the Pi itself. Keep the Pi's own password safe and "
        "update it now and then: sudo apt update &amp;&amp; sudo apt full-upgrade.",
    ])

    s.chapter("Where things are", appendix=True)
    s.table(["", "XAMPP (Windows / Mac)", "Raspberry Pi, Debian, Ubuntu"], [
        ["Station address", "IP address/artilody", "IP address"],
        ["Web pages", "http://IP/artilody", "http://IP"],
        ["Setup", "http://localhost/artilody/setup.php", "sudo bash install.sh"],
        ["Program files", "htdocs/artilody", "/var/www/artilody"],
        ["Configuration", "htdocs/artilody/config.php", "/etc/artilody/config.php"],
        ["Media files", "htdocs/artilody/public/blobs", "/var/lib/artilody/blobs"],
        ["Database backups", "phpMyAdmin › Export (by hand)", "/var/backups/artilody (automatic)"],
        ["Admin password", "setup.php (new setup)", "sudo artilody-admin-password"],
    ], [1.5, 2.5, 2.6])
    s.spacer(10)
    s.p(f'<font color="#5b6485">This manual describes the Artilody server of version {V}. Screenshots show the '
        "server with demo data.</font>")
