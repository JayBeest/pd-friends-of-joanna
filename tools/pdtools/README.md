# Perfect Dark Tooling

This repository contains several tools for working with the [Perfect Dark decompilation project](https://gitlab.com/RyanDwyer/perfect-dark).

Most tools are written for my own use only, and therefore contain little documentation and make assumptions about what's installed in the environment. They weren't designed to be shared, but are being shared now as they may be usable for other projects or serve as inspiration for making your own tooling.

Many of these tools use the following environment variables:

* `$PD` - is expected to exist and be a path to the perfect-dark repository root
* `$PDTOOLS` - is expected to exist and be a path to this repository root
* `$ROMID` - is optional (it defaults to `ntsc-final`) and determines which version of the ROM you are working with. Other values are `ntsc-beta`, `ntsc-1.0`, `pal-beta`, `pal-final` and `jap-final`.

It is recommended to set these variables in your shell's rc file (`~/.bashrc` for bash or `~/.zshrc` for zsh) along with adding the path to the tools directory to your `$PATH`, like so:

    export PATH="$PATH:$HOME/projects/pdtools/bin"
    export PD="$HOME/projects/perfect-dark"
    export PDTOOLS="$HOME/projects/pdtools"

## Available Tools

* [pd-addr](#pd-addr)
* [pd-addrshort](#pd-addrshort)
* [pd-cleanup](#pd-cleanup)
* [pd-const](#pd-const)
* [pd-convtext](#pd-convtext)
* [pd-diff](#pd-diff)
* [pd-diffcount](#pd-diffcount)
* [pd-diffreload](#pd-diffreload)
* [pd-dis](#pd-dis)
* [pd-float](#pd-float)
* [pd-funcat](#pd-funcat)
* [pd-gfx](#pd-gfx)
* [pd-mkunk](#pd-mkunk)
* [pd-parsemap](#pd-parsemap)
* [pd-rename](#pd-rename)
* [pd-resolveconstant](#pd-resolveconstant)
* [pd-savemap](#pd-savemap)
* [pd-sound](#pd-sound)
* [pd-stats](#pd-stats)
* [pd-struct](#pd-struct)
* [pd-text](#pd-text)
* [pd-todo](#pd-todo)
* [pd-watch](#pd-watch)
* [pd-wtf](#pd-wtf)

### pd-addr

Displays information about a memory address.

Example usage:

    $ pd-addr 8009a060
    address 0x8009a060
    symbol g_Vars offset 0xa0

You can also pass a negative address such as the ones Ghidra outputs and it'll figure it out:

    $ pd-addr -0x7ff5ce80
    address 0x800a3180
    symbol var800a3180

### pd-addrshort

Same as above, but outputs just the symbol name plus offset. It's intended to be used by ad hoc scripts which find and replace hard coded pointers or symbol names in ASM.

    $ pd-addrshort 8009a060
    g_Vars+0xa0

### pd-cleanup

Applies some basic regex replacements on code that has been imported from Ghidra. It's the first thing I do after pasting a function.

The tool is specific to my Ghidra setup and also to PD, so it would need amendments if used in other projects.

It's invoked using a vim keybinding:

    vnoremap ,i :!pd-cleanup<CR>

### pd-const

Searches `constants.h` for constants which start with the given prefix. Useful as a reference when decompiling code.

    $ pd-const crouch
    #define CROUCH_SQUAT 0
    #define CROUCH_HALF  1
    #define CROUCH_STAND 2

### pd-convtext

Converts hex text IDs to their constant form. It works by searching the input for text IDs, which allows a large chunk of code to be parsed by the tool in one go.

It's invoked using a vim keybinding:

    vnoremap ,l :!pd-convtext<CR>

Example usage (before text):

    u16 text1 = 0x5403;
    u16 text2 = 0x5404;

Visually select the lines, run the keybinding and the text is changed to the following:

    u16 text1 = L_MPWEAPONS(3);
    u16 text2 = L_MPWEAPONS(4);

You can also run it outside of vim by copying the text to your clipboard then running `echo '<paste here>' | pd-convtext`.

### pd-diff

Disassembles and displays a diff of the disassembly in an editor of your choice.

Example usage:

    make
    pd-diff chrGoToPad

This does the following:

* Inspects the generated map file at `build/$ROMID/pd.map` to find the start and end address of the given function, as well as determine which segment it's in (boot, lib or game).
* Saves the name of the function being diffed to a file at `/tmp/pd-func`. This is used when running `pd-watch` (see description for that command further below).
* Invokes `pd-dis` to disassemble the function in both the extracted binary and in the built binary. These are output to `/tmp/extracted.asm` and `/tmp/build.asm`.
* Invokes an editor of your choice to do the comparison. Defaults to `meld`, but you can pass `diffuse` or `nvim` as a second argument to the command to use those instead.

### pd-diffcount

Similar to the above, but just outputs the number of instructions that differ. Used by ad hoc brute force scripts.

### pd-diffreload

This is invoked by `pd-watch`. It is not meant to be run manually.

It reads `/tmp/pd-func` to determine which function is being diffed, then disassembles that function and updates `/tmp/build.asm`. This causes Meld to reload. See the description of `pd-watch` for more information.

### pd-dis

Disassembles a function then decorates the disassembly with more information to make decompilation easier. The result is printed to stdout.

The decorations include:

* Replacing jal addresses with function names
* Marking the start and end of loops
* Marking branch targets
* Replacing branch-likely delay slots with "DELAY"
* Replacing integer literals with hex values if >= 10
* Appending float values to lui instructions

It also contains some commented code which filters the output to only include branch instructions and/or jal instructions. This helps when starting to decompile a very large function as it helps you check that you've got the branching order correct.

This tool is not intended to be run manually. It's typically invoked by other commands.

### pd-float

Converts a hex literal to a float.

You can convert a single value:

    $ echo 0x42700000 | pd-float
    60

Or you can copy and paste a chunk of C code containing hex literals into the tool:

    echo '<paste here>' | pd-float

If using vim, you can set up a keybinding to allow you visually select code and convert it:

    vnoremap ,f :!pd-float<CR>

### pd-funcat

("func at", not "fun cat"...)

Takes a segment name and hex address as input and tells you what function is at that address.

The address is a memory address masked with 0x00ffffff. It is *not* an offset into that segment's binary.

Example usage in the lib segment:

    $ pd-funcat lib e324
    mainTick (lib/main.c)

Example usage in the game segment:

    $ pd-funcat game 107fb0
	filemgrGetDeviceName (game/game_107fb0.c)

The boot segment is not supported by this tool.

### pd-gfx

Takes a block of Ghidra-decompiled GBI instructions and converts it to proper GBI macros. Assumes gfxdis is installed (it's a third party project) and is in your `$PATH`.

For example, put this code in your clipboard:

    *param_1 = 0xe7000000;
    param_1[1] = 0;
    param_1[3] = 0;
    param_1[2] = 0xba001301;
    param_1[5] = 0;
    param_1[4] = 0xb9000002;
    param_1[7] = 0;
    param_1[6] = 0xba001001;

Then run the tool like this:

    echo '<paste here>' | pd-gfx

The tool outputs the gfxdis command it used along with the macros:

    gfxdis.f3d -g gdl -d e700000000000000ba00130100000000b900000200000000ba00100100000000
    gDPPipeSync(gdl++);
    gDPSetTexturePersp(gdl++, G_TP_NONE);
    gDPSetAlphaCompare(gdl++, G_AC_NONE);
    gDPSetTextureLOD(gdl++, G_TL_TILE);

The tool won't work if the input code uses variables in the macros. To convert those, I manually replace those variables with literal values such as 0x1111 and 0x2222, then run it through the tool, then replace those literal values with the variables again afterwards.

### pd-mkunk

Generates filler properties for a struct.

    $ pd-mkunk 0x20 0x30
    /*0x020*/ u32 unk020;
    /*0x024*/ u32 unk024;
    /*0x028*/ u32 unk028;
    /*0x02c*/ u32 unk02c;

The arguments are the start and stop address.

I call this from within vim using something like `:r! pd-mkunk 0 0x200`, but it also can be run in a terminal and copy/pasted into your editor.

### pd-parsemap

Parses an LD map file and outputs function and variable addresses as JSON. Used by other tools.

### pd-rename

Rename a symbol across the project.

    $ pd-rename oldname newname

The command takes care not to rename a symbol if its name *contains* the old name (such as oldname2).

### pd-resolveconstant

Takes code as input, searches for hex literals and replaces them with constants that were read from `constants.h`. This tool was probably used only once but remained in my collection because it might be useful in the future, so here it is.

Usage:

* Adjust the prefix variable in the tool's code. This should be the prefix of the constant name.
* Copy the source code containing hex literals to your clipboard.
* Run `echo '<paste here>' | pd-resolveconstant`.
* The source code will be printed to stdout with hex literals replaced with their constant values.

### pd-savemap

Runs `pd-parsemap` and saves its output to the tmp directory in this tools repository. I run this periodically when the project is matching.

The saved map is used by `pd-dis` to replace jal addresses with function names. For speed reasons, particularly when doing live reloading, it reads them from this file rather than reparsing the map each time.

### pd-sound

Plays a sound with mpv. The sound can be an MP3 from the ROM or an instrument sound effect.

    $ pd-sound 73d5
    Am6_l2_bM

The above plays Elvis's unused line in Deep Sea about antibody masking.

The instrument sounds (wav files) should be extracted in advance using a sound ripper and placed in the tmp/wav directory in this repository. The MP3s should work fine though.

Some instrument files may not play at the correct speed and pitch. The game determines this in a config array but this tool doesn't go that far as it's only intended to identify the sound. You can adjust the speed by editing the tool and changing mpv's `--speed` option to another number. It's set as 0.25 which seems like the most common speed that was used.

### pd-stats

Calculates decomp completion statistics. They are the same calculations that are used on the project's status page, so they produce the same values.

    $ pd-stats
    boot         284/  2,044   13.89%
    lib        5,080/ 81,168    6.26%
    inflate    1,204/  1,204  100.00%
    game     111,439/433,712   25.69%
    TOTAL    118,007/518,128   22.78%

### pd-struct

Looks up a struct definition and prints it to stdout, to save me having to search for it in `types.h`.

    $ pd-struct pad
    // line 75:
    struct pad {
        /*0x00*/ struct coord pos;
        /*0x0c*/ struct coord look;
        /*0x18*/ struct coord up;
        /*0x24*/ struct coord normal;
        /*0x30*/ struct bbox bbox;
        /*0x48*/ s32 room;
        /*0x4c*/ u32 flags;
        /*0x50*/ u8 liftnum; // 1-10, 0 indicates no lift
        /*0x52*/ s16 unk52;
    };

### pd-text

Reads a text ID and outputs the text that it resolves to.

    $ pd-text 4e96
    perfect dark is forever\n

### pd-todo

Prints a list of functions that are not yet decompiled. Results are sorted by size. Filters can be applied using certain arguments.

Example output:

    func0f11deb8 len 34 (src/game/pak/pak.c:8715)
    func0f11bbd8 len 33 (src/game/pak/pak.c:6228)
    func0f11d4dc len 27 (src/game/pak/pak.c:8036)

A filename or folder can be passed to only search that path:

    $ pd-todo src/game/pak/pak.c

Filters can be passed to further limit the functions returned:

* `--aicmdchild`: Filters functions to those that are called from `chraicommands.c`.
* `--attempted`: Filters functions to those where a decompilation has been attempted, based on comments above the function.
* `--callbacks`: Filters functions to those which execute callbacks, such as functions which invoke menu handlers.
* `--custom`: Used for temporary filters, where I write the logic into the handler then run it.
* `--data`: Filters functions to those which use .data or .bss.
* `--ifs`: Filters functions to those which contain branch statements.
* `--jals`: Filters functions to those which call other functions.
* `--loops`: Filters functions to those which contain loops.
* `--rodata`: Filters functions to those which work with rodata.
* `--render`: Filters functions to those which are likely starting a display list.
* `--switches`: Filters functions to those which contain switches.
* `--knownargs`: Filters functions to those which contain known arguments.
* `--structargs`: Filters functions to those which contain known arguments and at least one of those is a struct.

All filters can be inversed by prefixing it with `--no-`, such as `--no-jals`. Filters can also be combined by adding them as additional arguments.

### pd-watch

Watches a source file for changes then reloads the Meld diff without any interaction, and keeps the scroll position in Meld.

There are a few pieces to this, including patching Meld to make it behave as desired.

1. In one terminal run `pd-diff funcname` to open Meld with a diff of your function.
2. In a second terminal, run `pd-watch src/game/whatever.c` where whatever.c is the file containing the function you're decompiling.
3. Make changes to the function source and save the file.
4. `pd-watch` will build the appropriate segment, then execute `pd-diffreload` which disassembles the segment and replaces the build file which Meld is showing.
5. Meld automatically detects this change and prompts you to reload the file, unless the patch is applied in which case it reloads it without prompting.
6. After reloading, Meld scrolls back to the top, unless the patch is applied which makes it keep its scroll position.

To patch Meld, you'll want to run something similar to the following:

    sudo patch -d /usr/lib/python3.8/site-packages/meld < meld-autoreload.patch

Adjust the directory as needed, and you may also need to adjust the patch file if the Meld project makes conflicting changes.

Whenever Meld is updated you'll likely need to reapply the patch.

Note that if you use `pd-rename` while running `pd-watch`, the file gets replaced with a new file which causes the watch to stop detecting changes. In this case you can just cancel the watch and restart it.

Also note that when you open Meld, the size of the extracted function is assumed to be the same size as your built function, and then the extracted (left) pane is not reloaded for the duration of the session. This means if your function changes size as you're making it match then you might find some misalignment/scrolling issues towards the bottom of the diffs. This is easily resolved; just close and reopen Meld once you have your function at the correct size.

### pd-wtf

Diffs an entire binary segment. Use when `make test` shows a mismatch and you don't know why.

The diff is generated by using `xxd` on both the extracted and build segments, then passing those to `diff`. The output is piped to `less`.
