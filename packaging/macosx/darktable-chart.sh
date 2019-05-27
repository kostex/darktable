#!/bin/sh

version="$(sw_vers -productVersion)".
major_v="${version%%.*}"
minor_v="${version#*.}"
minor_v="${minor_v%%.*}"
if [ -z "$minor_v" ]
then
    minor_v=0
fi
if [ "$major_v" -lt 10 ] || ( [ "$major_v" -eq 10 ] && [ "$minor_v" -lt 7 ] )
then
    osascript -e 'tell app (path to frontmost application as text) to display dialog "darktable: unsupported macOS version (at least 10.7 is required)!" buttons {"Close"} with icon stop'
    exit 1
fi

if test -n "$GTK_DEBUG_LAUNCHER"; then
    set -x
fi

name="$(basename "$0")"
pushd "$(dirname "$0")" >/dev/null
bundle_contents="$(dirname "$PWD")"
popd > /dev/null
bundle_res="$bundle_contents"/Resources
bundle_lib="$bundle_res"/lib
bundle_bin="$bundle_res"/bin
bundle_data="$bundle_res"/share
bundle_etc="$bundle_res"/etc

export XDG_CONFIG_DIRS="$bundle_etc"
export XDG_DATA_DIRS="$bundle_data"
export GTK_DATA_PREFIX="$bundle_res"
export GTK_EXE_PREFIX="$bundle_res"
export GTK_PATH="$bundle_res"

export GTK_IM_MODULE_FILE="$bundle_etc"/gtk-3.0/gtk.immodules
export GDK_PIXBUF_MODULE_FILE="$(echo "$bundle_lib"/gdk-pixbuf-2.0/*/loaders.cache)"
export GSETTINGS_SCHEMA_DIR="$bundle_data"/glib-2.0/schemas

export IOLIBS="$(echo "$bundle_lib"/libgphoto2_port/*/)"
export CAMLIBS="$(echo "$bundle_lib"/libgphoto2/*/)"

APP=darktable
I18NDIR="$bundle_data"/locale
# Set the locale-related variables appropriately:
unset LANG LC_MESSAGES LC_COLLATE

# Has a language ordering been set?
# If so, set LC_MESSAGES and LANG accordingly; otherwise skip it.
# First step uses sed to clean off the quotes and commas, to change - to _, and change the names for the chinese scripts from "Hans" to CN and "Hant" to TW.
APPLELANGUAGES="$(defaults read .GlobalPreferences AppleLanguages | sed -En -e 's/-/_/' -e 's/Hant/TW/' -e 's/Hans/CN/' -e 's/[[:space:]]*"?([[:alnum:]_]+)"?,?/\1/p')"
if test -n "$APPLELANGUAGES"; then
    # A language ordering exists.
    # Test, item per item, to see whether there is an corresponding locale.
    for L in $APPLELANGUAGES; do
        #test for exact matches:
        if test -f "$I18NDIR"/"$L"/LC_MESSAGES/"$APP".mo; then
            export LANG="$L"
            break
        fi
        #This is a special case, because often the original strings are in US
        #English and there is no translation file.
        if test "$L" = en_US; then
            export LANG="$L"
            break
        fi
        #OK, now test for just the first two letters:
        if test -f "$I18NDIR"/"${L:0:2}"/LC_MESSAGES/"$APP".mo; then
            export LANG="${L:0:2}"
            break
        fi
        #Same thing, but checking for any english variant.
        if test "${L:0:2}" = en; then
            export LANG="$L"
            break
        fi
    done  
fi
unset APPLELANGUAGES L

# If we didn't get a language from the language list, try the Collation preference, in case it's the only setting that exists.
APPLECOLLATION="$(defaults read .GlobalPreferences AppleCollationOrder 2>/dev/null | sed -e 's/root/C/')"
if test -z "$LANG" -a -n "$APPLECOLLATION"; then
    if test -f "$I18NDIR"/"${APPLECOLLATION:0:2}"/LC_MESSAGES/"$APP".mo; then
        export LANG="${APPLECOLLATION:0:2}"
    fi
fi
if test -n "$APPLECOLLATION"; then
    export LC_COLLATE="$APPLECOLLATION"
fi
unset APPLECOLLATION

# Continue by attempting to find the Locale preference.
APPLELOCALE="$(defaults read .GlobalPreferences AppleLocale)"

if test -z "$LANG"; then 
    if test -f "$I18NDIR"/"${APPLELOCALE:0:5}"/LC_MESSAGES/"$APP".mo; then
        export LANG="${APPLELOCALE:0:5}"
    elif test -f "$I18NDIR"/"${APPLELOCALE:0:2}"/LC_MESSAGES/"$APP".mo; then
        export LANG="${APPLELOCALE:0:2}"
    fi
fi

#Next we need to set LC_MESSAGES. If at all possible, we want a full
#5-character locale to avoid the "Locale not supported by C library"
#warning from Gtk -- even though Gtk will translate with a
#two-character code.
if test -n "$LANG"; then 
    #If the language code matches the applelocale, then that's the message
    #locale; otherwise, if it's longer than two characters, then it's
    #probably a good message locale and we'll go with it.
    #Next try if the Applelocale is longer than 2 chars and the language
    #bit matches $LANG
    if test "$LANG" = "${APPLELOCALE:0:2}" -a "$APPLELOCALE" != "${APPLELOCALE:0:2}"; then
        export LANG="${APPLELOCALE:0:5}"
    #Fail. Get a list of the locales in $PREFIX/share/locale that match
    #our two letter language code and pick the first one, special casing
    #english to set en_US
    elif test "$LANG" = en; then
        export LANG=en_US
    else
        LOC="$(basename -a "$I18NDIR"/"$LANG"???/)"
        for L in $LOC; do
            if test -d "$I18NDIR"/"$L"; then
                export LANG="$L"
                break
            fi
        done
    fi
else
    #All efforts have failed, so default to US english
    export LANG=en_US
fi

unset APPLELOCALE LOC L

if test -f "$bundle_lib"/charset.alias; then
    export CHARSETALIASDIR="$bundle_lib"
fi

# Extra arguments can be added in environment.sh.
if test -f "$bundle_res"/environment.sh; then
    source "$bundle_res"/environment.sh
fi

# Strip out the argument added by the OS.
if expr "$1" : '^-psn_' > /dev/null; then
    shift 1
fi

exec $GTK_DEBUG_GDB "$bundle_contents"/MacOS/"$name"-bin $EXTRA_ARGS "$@"
