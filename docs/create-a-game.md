# Create a game

Make a new game on a blank floppy disk: Towns OS formats it, and whatever you put on it (a program you write, a game's user disk, files you install) is kept as a game in your library. The Towns OS or application CD you boot from can stay wherever it is (for example in Download): the Disks page puts it in the CD-ROM from anywhere on the device.

## 1. Make a blank disk

Open the library menu › **Manage library**. Under **New game**, choose **Floppy disk · 1.2 MB · 2HD**. The hard disks (40, 100 and 240 MB SCSI) are made the same way; Towns OS's own hard disk setup prepares one.

![New game](screenshots/create-a-game/01-new-game.png)

Name it and press **Create**.

![Naming the new game](screenshots/create-a-game/02-name.png)

The blank disk is saved in your games folder, in a folder of its own, and listed as a game.

![The new game in the library](screenshots/create-a-game/03-listed.png)

## 2. Start Towns OS with the new disk in its drive

Press **Play**. The blank disk is in Floppy A, and with nothing to start from the Towns says so (システムが違います).

![No system on the blank disk](screenshots/create-a-game/04-not-a-system-disk.png)

Open the menu › **Disks** › **CD-ROM** › **Choose file…** and pick a Towns OS CD, or an application CD that starts Towns OS (here F-BASIC386). A CD kept as a CUE sheet with its BIN goes in as an archive that holds both: a ZIP of the two files.

![Choosing the CD](screenshots/create-a-game/05-choose-cd.png)

![The CD in the CD-ROM, the new disk in Floppy A](screenshots/create-a-game/06-disks.png)

Towns OS starts from the CD.

![Towns OS](screenshots/create-a-game/07-towns-os.png)

## 3. Format it

In Towns MENU, **ディスク** › **初期化** (Disk › Initialize): drive A, **1.2 MB [2HD]**, a name for the disk, then **実行** (Run) and **実行** again to confirm.

![Initialize](screenshots/create-a-game/08-initialize.png)

![Formatted](screenshots/create-a-game/09-formatted.png)

Double-tap drive A's icon to open it: an empty Towns disk.

![The formatted disk](screenshots/create-a-game/10-drive-a.png)

## 4. Put your game on it

Install to it or save to it from any Towns program. Here: start F-BASIC386 from the CD, type a program,

![A program](screenshots/create-a-game/11-program.png)

then **ファイル** › **保存** (File › Save) as `A:¥HELLO.BAS`.

![Saving to drive A](screenshots/create-a-game/12-save-to-a.png)

![Saved](screenshots/create-a-game/13-saved.png)

## 5. It is kept

The disk keeps what Towns OS and your programs write to it. After closing and reopening KairoTowns, the new game is still in the library, its disk still holds its files, and it plays like any other game.

![The new game after reopening](screenshots/create-a-game/14-kept.png)
