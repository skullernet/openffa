/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "q_shared.h"

vec3_t vec3_origin = { 0, 0, 0 };

void AngleVectors (vec3_t angles, vec3_t forward, vec3_t right, vec3_t up)
{
    float        angle;
    float        sr, sp, sy, cr, cp, cy;

    angle = angles[YAW] * (M_PI*2 / 360);
    sy = sin(angle);
    cy = cos(angle);
    angle = angles[PITCH] * (M_PI*2 / 360);
    sp = sin(angle);
    cp = cos(angle);
    angle = angles[ROLL] * (M_PI*2 / 360);
    sr = sin(angle);
    cr = cos(angle);

    if (forward)
    {
        forward[0] = cp*cy;
        forward[1] = cp*sy;
        forward[2] = -sp;
    }
    if (right)
    {
        right[0] = (-1*sr*sp*cy+-1*cr*-sy);
        right[1] = (-1*sr*sp*sy+-1*cr*cy);
        right[2] = -1*sr*cp;
    }
    if (up)
    {
        up[0] = (cr*sp*cy+-sr*-sy);
        up[1] = (cr*sp*sy+-sr*cy);
        up[2] = cr*cp;
    }
}

vec_t VectorNormalize (vec3_t v)
{
    float    length, ilength;

    length = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
    length = sqrt (length);        // FIXME

    if (length)
    {
        ilength = 1/length;
        v[0] *= ilength;
        v[1] *= ilength;
        v[2] *= ilength;
    }
        
    return length;

}

vec_t VectorNormalize2 (vec3_t v, vec3_t out)
{
    float    length, ilength;

    length = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
    length = sqrt (length);        // FIXME

    if (length)
    {
        ilength = 1/length;
        out[0] = v[0]*ilength;
        out[1] = v[1]*ilength;
        out[2] = v[2]*ilength;
    }
        
    return length;

}

void AddPointToBounds (const vec3_t v, vec3_t mins, vec3_t maxs) {
    int        i;
    vec_t    val;

    for (i=0 ; i<3 ; i++)
    {
        val = v[i];
        if (val < mins[i])
            mins[i] = val;
        if (val > maxs[i])
            maxs[i] = val;
    }
}

//====================================================================================

/*
==================
COM_IsFloat

Returns true if the given string is valid representation
of floating point number.
==================
*/
qboolean COM_IsFloat( const char *s ) {
    int c, dot = '.';

    if( *s == '-' ) {
        s++;
    }
    if( !*s ) {
        return qfalse;
    }

    do {
        c = *s++;
        if( c == dot ) {
            dot = 0;
        } else if( !Q_isdigit( c ) ) {
            return qfalse;
        }
    } while( *s );

    return qtrue;
}

qboolean COM_IsUint( const char *s ) {
    int c;

    if( !*s ) {
        return qfalse;
    }

    do {
        c = *s++;
        if( !Q_isdigit( c ) ) {
            return qfalse;
        }
    } while( *s );

    return qtrue;
}

qboolean COM_HasSpaces( const char *s ) {
    while( *s ) {
        if( *s <= 32 ) {
            return qtrue;
        }
        s++;
    }
    return qfalse;
}

int QDECL SortStrcmp( const void *p1, const void *p2 ) {
    return strcmp( *( const char ** )p1, *( const char ** )p2 );
}

int QDECL SortStricmp( const void *p1, const void *p2 ) {
    return Q_stricmp( *( const char ** )p1, *( const char ** )p2 );
}

int Q_HighlightStr( char *out, const char *in, int bufsize ) {
    char *p, *m;
    int c;

    if( bufsize < 1 ) {
        Com_Error( ERR_FATAL, "%s: bad bufsize: %d", __func__, bufsize );
    }

    p = out;
    m = out + bufsize - 1;
    while( *in && p < m ) {
        c = *in++;
        c |= 128;
        *p++ = c;
    }

    *p = 0;

    return p - out;
}

/*
================
Q_IsWhiteSpace
================
*/
qboolean Q_IsWhiteSpace( const char *s ) {
    int c;

    while( *s ) {
        c = *s++;
        c &= 127;
        if( Q_isgraph( c ) ) {
            return qfalse;
        }
    }

    return qtrue;
}

/*
============
va

does a varargs printf into a temp buffer, so I don't need to have
varargs versions of all text functions.
FIXME: make this buffer size safe someday
============
*/
char *va( const char *format, ... ) {
    va_list         argptr;
    static char     buffers[2][0x2800];
    static int      index;

    index ^= 1;
    
    va_start( argptr, format );
    Q_vsnprintf( buffers[index], sizeof( buffers[0] ), format, argptr );
    va_end( argptr );

    return buffers[index];    
}

static char     com_token[4][MAX_TOKEN_CHARS];
static int      com_tokidx;

/*
==============
COM_Parse

Parse a token out of a string.
Handles C and C++ comments.
==============
*/
char *COM_Parse( const char **data_p ) {
    int         c;
    int         len;
    const char  *data;
    char        *s = com_token[com_tokidx++ & 3];

    data = *data_p;
    len = 0;
    s[0] = 0;
    
    if( !data ) {
        *data_p = NULL;
        return s;
    }
        
// skip whitespace
skipwhite:
    while( ( c = *data ) <= ' ' ) {
        if( c == 0 ) {
            *data_p = NULL;
            return s;
        }
        data++;
    }
    
// skip // comments
    if( c == '/' && data[1] == '/' ) {
        data += 2;
        while( *data && *data != '\n' )
            data++;
        goto skipwhite;
    }

// skip /* */ comments
    if( c == '/' && data[1] == '*' ) {
        data += 2;
        while( *data ) {
            if( data[0] == '*' && data[1] == '/' ) {
                data += 2;
                break;
            }
            data++;
        }
        goto skipwhite;
    }

// handle quoted strings specially
    if( c == '\"' ) {
        data++;
        while( 1 ) {
            c = *data++;
            if( c == '\"' || !c ) {
                goto finish;
            }

            if( len < MAX_TOKEN_CHARS - 1 ) {
                s[len++] = c;
            }
        }
    }

// parse a regular word
    do {
        if( len < MAX_TOKEN_CHARS - 1 ) {
            s[len++] = c;
        }
        data++;
        c = *data;
    } while( c > 32 );

finish:
    s[len] = 0;

    *data_p = data;
    return s;
}

/*
==============
COM_Compress

Operates in place, removing excess whitespace and comments.
Non-contiguous line feeds are preserved.

Returns resulting data length.
==============
*/
int COM_Compress( char *data ) {
    int     c, n = 0;
    char    *s = data, *d = data;

    while( *s ) {
        // skip whitespace    
        if( *s <= ' ' ) {
            n = ' ';
            do {
                c = *s++;
                if( c == '\n' ) {
                    n = '\n';
                }
                if( !c ) {
                    goto finish;
                }
            } while( *s <= ' ' );
        }
        
        // skip // comments
        if( s[0] == '/' && s[1] == '/' ) {
            n = ' ';
            s += 2;
            while( *s && *s != '\n' ) {
                s++;
            }
            continue;
        }

        // skip /* */ comments
        if( s[0] == '/' && s[1] == '*' ) {
            n = ' ';
            s += 2;
            while( *s ) {
                if( s[0] == '*' && s[1] == '/' ) {
                    s += 2;
                    break;
                }
                s++;
            }
            continue;
        }

        // add whitespace character
        if( n ) {
            *d++ = n;
            n = 0;
        }

        // handle quoted strings specially
        if( *s == '\"' ) {
            s++;
            *d++ = '\"';
            do {
                c = *s++;
                if( !c ) {
                    goto finish;
                }
                *d++ = c;
            } while( c != '\"' );
            continue;
        }

        // handle line feed escape
        if( *s == '\\' && s[1] == '\n' ) {
            s += 2;
            continue;
        }

        // parse a regular word
        do {
            *d++ = *s++;
        } while( *s > ' ' );
    }

finish:
    *d = 0;

    return d - data;
}

/*
============================================================================

                    LIBRARY REPLACEMENT FUNCTIONS

============================================================================
*/

int Q_strncasecmp( const char *s1, const char *s2, size_t n ) {
    int        c1, c2;
    
    do {
        c1 = *s1++;
        c2 = *s2++;

        if( !n-- )
            return 0;        /* strings are equal until end point */
        
        if( c1 != c2 ) {
            c1 = Q_tolower( c1 );
            c2 = Q_tolower( c2 );
            if( c1 < c2 )
                return -1;
            if( c1 > c2 )
                return 1;        /* strings not equal */
        }
    } while( c1 );
    
    return 0;        /* strings are equal */
}

int Q_strcasecmp( const char *s1, const char *s2 ) {
    int        c1, c2;
    
    do {
        c1 = *s1++;
        c2 = *s2++;
        
        if( c1 != c2 ) {
            c1 = Q_tolower( c1 );
            c2 = Q_tolower( c2 );
            if( c1 < c2 )
                return -1;
            if( c1 > c2 )
                return 1;        /* strings not equal */
        }
    } while( c1 );
    
    return 0;        /* strings are equal */
}

char *Q_strcasestr( const char *s1, const char *s2 ) {
    size_t l1, l2;

    l2 = strlen( s2 );
    if( !l2 ) {
        return ( char * )s1;
    }

    l1 = strlen( s1 );
    while( l1 >= l2 ) {
        l1--;
        if( !Q_strncasecmp( s1, s2, l2 ) ) {
            return ( char * )s1;
        }
        s1++;
    }

    return NULL;
}

/*
===============
Q_strlcpy

Returns length of the source string.
===============
*/
size_t Q_strlcpy( char *dst, const char *src, size_t size ) {
    size_t ret = strlen( src );

    if( size ) {
        size_t len = ret >= size ? size - 1 : ret;
        memcpy( dst, src, len );
        dst[len] = 0;
    }

    return ret;
}

/*
===============
Q_strlcat

Returns length of the source and destinations strings combined.
===============
*/
size_t Q_strlcat( char *dst, const char *src, size_t size ) {
    size_t ret, len = strlen( dst );

    if( len >= size ) {
        Com_Error( ERR_FATAL, "%s: already overflowed", __func__ );
    }

    ret = Q_strlcpy( dst + len, src, size - len );
    ret += len;

    return ret;
}

/*
===============
Q_concat

Returns number of characters that would be written into the buffer,
excluding trailing '\0'. If the returned value is equal to or greater than
buffer size, resulting string is truncated.
===============
*/
size_t Q_concat( char *dest, size_t size, ... ) {
    va_list argptr;
    const char *s;
    size_t len, total = 0;

    va_start( argptr, size );
    while( ( s = va_arg( argptr, const char * ) ) != NULL ) {
        len = strlen( s );
        if( total + len < size ) {
            memcpy( dest, s, len );
            dest += len;
        }
        total += len;
    }
    va_end( argptr );

    if( size ) {
        *dest = 0;
    }

    return total;
}

/*
===============
Q_vsnprintf

Returns number of characters that would be written into the buffer,
excluding trailing '\0'. If the returned value is equal to or greater than
buffer size, resulting string is truncated.
===============
*/
size_t Q_vsnprintf( char *dest, size_t size, const char *fmt, va_list argptr ) {
    int ret;

    if( size > INT_MAX ) {
        Com_Error( ERR_FATAL, "%s: bad buffer size", __func__ );
    }

#ifdef _WIN32
    // work around broken M$ C runtime semantics.
    if( !size ) {
        return 0;
    }
    ret = _vsnprintf( dest, size - 1, fmt, argptr );
    if( ret >= size - 1 ) {
        dest[size - 1] = 0;
    }
#else
    ret = vsnprintf( dest, size, fmt, argptr );
#endif

    // this shouldn't happen
    if( ret < 0 ) {
        if( size ) {
            *dest = 0;
        }
        ret = 0;
    }

    return ( size_t )ret;
}

/*
===============
Q_vscnprintf

Returns number of characters actually written into the buffer,
excluding trailing '\0'. If buffer size is 0, this function does nothing
and returns 0.
===============
*/
size_t Q_vscnprintf( char *dest, size_t size, const char *fmt, va_list argptr ) {
    size_t ret;
    
    if( !size ) {
        return 0;
    }

    ret = Q_vsnprintf( dest, size, fmt, argptr );
    return ret >= size ? size - 1 : ret;
}


/*
===============
Q_snprintf

Returns number of characters that would be written into the buffer,
excluding trailing '\0'. If the returned value is equal to or greater than
buffer size, resulting string is truncated.
===============
*/
size_t Q_snprintf( char *dest, size_t size, const char *fmt, ... ) {
    va_list argptr;
    size_t  ret;

    va_start( argptr, fmt );
    ret = Q_vsnprintf( dest, size, fmt, argptr );
    va_end( argptr );

    return ret;
}

/*
===============
Q_scnprintf

Returns number of characters actually written into the buffer,
excluding trailing '\0'. If buffer size is 0, this function does nothing
and returns 0.
===============
*/
size_t Q_scnprintf( char *dest, size_t size, const char *fmt, ... ) {
    va_list argptr;
    size_t  ret;

    va_start( argptr, fmt );
    ret = Q_vscnprintf( dest, size, fmt, argptr );
    va_end( argptr );

    return ret;
}

char *Q_strchrnul( const char *s, int c ) {
    while( *s && *s != c ) {
        s++;
    }
    return ( char * )s;
}

void Q_setenv( const char *name, const char *value ) {
#ifdef _WIN32
#ifndef __COREDLL__
    if( !value ) {
        value = "";
    }
#if( _MSC_VER >= 1400 )
    _putenv_s( name, value );
#else
    _putenv( va( "%s=%s", name, value ) );
#endif
#endif
#else // _WIN32
    if( value ) {
        setenv( name, value, 1 );
    } else {
        unsetenv( name );
    }
#endif // !_WIN32
}

/*
=====================================================================

  INFO STRINGS

=====================================================================
*/

/*
===============
Info_ValueForKey

Searches the string for the given
key and returns the associated value, or an empty string.
===============
*/
char *Info_ValueForKey( const char *s, const char *key ) {
    static char value[4][MAX_INFO_STRING];  // use 4 buffers so compares
                                            // work without stomping on each other
    static int  valueindex;
    char        pkey[MAX_INFO_STRING];
    char        *o;

    valueindex++;
    if( *s == '\\' )
        s++;
    while( 1 ) {
        o = pkey;
        while( *s != '\\' ) {
            if( !*s )
                return "";
            *o++ = *s++;
        }
        *o = 0;
        s++;

        o = value[valueindex & 3];
        while( *s != '\\' && *s ) {
            *o++ = *s++;
        }
        *o = 0;

        if( !strcmp( key, pkey ) )
            return value[valueindex & 3];

        if( !*s )
            return "";
        s++;
    }

    return "";
}

/*
==================
Info_RemoveKey
==================
*/
void Info_RemoveKey( char *s, const char *key ) {
    char    *start;
    char    pkey[MAX_INFO_STRING];
    char    *o;

    while( 1 ) {
        start = s;
        if( *s == '\\' )
            s++;
        o = pkey;
        while( *s != '\\' ) {
            if( !*s )
                return;
            *o++ = *s++;
        }
        *o = 0;
        s++;

        while( *s != '\\' && *s ) {
            s++;
        }

        if( !strcmp( key, pkey ) ) {
            o = start; // remove this part
            while( *s ) {
                *o++ = *s++;
            }
            *o = 0;
            s = start;
            continue; // search for duplicates
        }

        if( !*s )
            return;
    }

}


/*
==================
Info_Validate

Some characters are illegal in info strings because they
can mess up the server's parsing.
Also checks the length of keys/values and the whole string.
==================
*/
qboolean Info_Validate( const char *s ) {
    size_t len, total;
    int c;
    
    total = 0;
    while( 1 ) {
        //
        // validate key
        //
        if( *s == '\\' ) {
            s++;
            if( ++total == MAX_INFO_STRING ) {
                return qfalse;    // oversize infostring
            }
        }
        if( !*s ) {
            return qfalse;    // missing key
        }
        len = 0;
        while( *s != '\\' ) {
            c = *s++;
            if( !Q_isprint( c ) || c == '\"' || c == ';' ) {
                return qfalse;    // illegal characters
            }
            if( ++len == MAX_INFO_KEY ) {
                return qfalse;    // oversize key
            }
            if( ++total == MAX_INFO_STRING ) {
                return qfalse;    // oversize infostring
            }
            if( !*s ) {
                return qfalse;    // missing value
            }
        }

        //
        // validate value
        //
        s++;
        if( ++total == MAX_INFO_STRING ) {
            return qfalse;    // oversize infostring
        }
        if( !*s ) {
            return qfalse;    // missing value
        }
        len = 0;
        while( *s != '\\' ) {
            c = *s++;
            if( !Q_isprint( c ) || c == '\"' || c == ';' ) {
                return qfalse;    // illegal characters
            }
            if( ++len == MAX_INFO_VALUE ) {
                return qfalse;    // oversize value
            }
            if( ++total == MAX_INFO_STRING ) {
                return qfalse;    // oversize infostring
            }
            if( !*s ) {
                return qtrue;    // end of string
            }
        }
    }

    return qfalse; // quiet compiler warning
}

/*
============
Info_SubValidate
============
*/
size_t Info_SubValidate( const char *s ) {
    size_t len;
    int c;

    len = 0;
    while( *s ) {
        c = *s++;
        c &= 127;       // strip high bits
        if( c == '\\' || c == '\"' || c == ';' ) {
            return SIZE_MAX;  // illegal characters
        }
        if( ++len == MAX_QPATH ) {
            return MAX_QPATH;  // oversize value
        }
    }

    return len;
}

/*
==================
Info_SetValueForKey
==================
*/
qboolean Info_SetValueForKey( char *s, const char *key, const char *value ) {
    char    newi[MAX_INFO_STRING], *v;
    size_t  l, kl, vl;
    int     c;

    // validate key
    kl = Info_SubValidate( key );
    if( kl >= MAX_QPATH ) {
        return qfalse;
    }

    // validate value
    vl = Info_SubValidate( value );
    if( vl >= MAX_QPATH ) {
        return qfalse;
    }

    Info_RemoveKey( s, key );
    if( !vl ) {
        return qtrue;
    }

    l = strlen( s );
    if( l + kl + vl + 2 >= MAX_INFO_STRING ) {
        return qfalse;
    }

    newi[0] = '\\';
    memcpy( newi + 1, key, kl );
    newi[kl + 1] = '\\';
    memcpy( newi + kl + 2, value, vl + 1 );

    // only copy ascii values
    s += l;
    v = newi;
    while( *v ) {
        c = *v++;
        c &= 127;        // strip high bits
        if( Q_isprint( c ) )
            *s++ = c;
    }
    *s = 0;

    return qtrue;
}

/*
==================
Info_NextPair
==================
*/
void Info_NextPair( const char **string, char *key, char *value ) {
    char        *o;
    const char  *s;

    *value = *key = 0;

    s = *string;
    if( !s ) {
        return;
    }

    if( *s == '\\' )
        s++;

    if( !*s ) {
        *string = NULL;
        return;
    }
    
    o = key;
    while( *s && *s != '\\' ) {
        *o++ = *s++;
    }
    
    *o = 0;

    if( !*s ) {
        *string = NULL;
        return;
    }

    o = value;
    s++;
    while( *s && *s != '\\' ) {
        *o++ = *s++;
    }
    *o = 0;

    if( *s ) {
        s++;
    }

    *string = s;
    
}

/*
==================
Info_Print
==================
*/
void Info_Print( const char *infostring ) {
    char    key[MAX_INFO_STRING];
    char    value[MAX_INFO_STRING];

    while( infostring ) {
        Info_NextPair( &infostring, key, value );
        
        if( !key[0] ) {
            break;
        }

        if( value[0] ) {
            Com_Printf( "%-20s %s\n", key, value );
        } else {
            Com_Printf( "%-20s <MISSING VALUE>\n", key );
        }
    }
}

