
// https://stackoverflow.com/questions/32972539/gdk-x11-screen-capture

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <sys/shm.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#ifdef __linux__
    #include <sys/time.h>
#endif

// comment the next line to busy-wait at each frame
//#define __SLEEP__
#define FRAME  16667
#define PERIOD 1000000
#define NAME   "screencap"
#define NAMESP "         "
#define BPP    4

struct shmimage
{
    XShmSegmentInfo shminfo ;
    XImage * ximage ;
    unsigned int * data ; // will point to the image's BGRA packed pixels
} ;

void initimage( struct shmimage * image )
{
    image->ximage = NULL ;
    image->shminfo.shmaddr = (char *) -1 ;
}

void destroyimage( Display * dsp, struct shmimage * image )
{
    if( image->ximage )
    {
        XShmDetach( dsp, &image->shminfo ) ;
        XDestroyImage( image->ximage ) ;
        image->ximage = NULL ;
    }

    if( image->shminfo.shmaddr != ( char * ) -1 )
    {
        shmdt( image->shminfo.shmaddr ) ;
        image->shminfo.shmaddr = ( char * ) -1 ;
    }
}

int createimage( Display * dsp, struct shmimage * image, int width, int height )
{
    // Create a shared memory area 
    image->shminfo.shmid = shmget( IPC_PRIVATE, width * height * BPP, IPC_CREAT | 0600 ) ;
    if( image->shminfo.shmid == -1 )
    {
        perror( NAME ) ;
        return false ;
    }

    // Map the shared memory segment into the address space of this process
    image->shminfo.shmaddr = (char *) shmat( image->shminfo.shmid, 0, 0 ) ;
    if( image->shminfo.shmaddr == (char *) -1 )
    {
        perror( NAME ) ;
        return false ;
    }

    image->data = (unsigned int*) image->shminfo.shmaddr ;
    image->shminfo.readOnly = false ;

    // Mark the shared memory segment for removal
    // It will be removed even if this program crashes
    shmctl( image->shminfo.shmid, IPC_RMID, 0 ) ;

    // Allocate the memory needed for the XImage structure
    image->ximage = XShmCreateImage( dsp, XDefaultVisual( dsp, XDefaultScreen( dsp ) ),
                        DefaultDepth( dsp, XDefaultScreen( dsp ) ), ZPixmap, 0,
                        &image->shminfo, 0, 0 ) ;
    if( !image->ximage )
    {
        destroyimage( dsp, image ) ;
        printf( NAME ": could not allocate the XImage structure\n" ) ;
        return false ;
    }

    image->ximage->data = (char *)image->data ;
    image->ximage->width = width ;
    image->ximage->height = height ;

    // Ask the X server to attach the shared memory segment and sync
    XShmAttach( dsp, &image->shminfo ) ;
    XSync( dsp, false ) ;
    return true ;
}

void getrootwindow( Display * dsp, struct shmimage * image )
{
    XShmGetImage( dsp, XDefaultRootWindow( dsp ), image->ximage, 0, 0, AllPlanes ) ;
}

long timestamp( )
{
   struct timeval tv ;
   struct timezone tz ;
   gettimeofday( &tv, &tz ) ;
   return tv.tv_sec*1000000L + tv.tv_usec ;
}

Window createwindow( Display * dsp, int width, int height )
{
    unsigned long mask = CWBackingStore ;
    XSetWindowAttributes attributes ;
    attributes.backing_store = NotUseful ;
    mask |= CWBackingStore ;
    Window window = XCreateWindow( dsp, DefaultRootWindow( dsp ),
            0, 0, width, height, 0,
            DefaultDepth( dsp, XDefaultScreen( dsp ) ),
            InputOutput, CopyFromParent, mask, &attributes ) ;
    XStoreName( dsp, window, NAME );
    XSelectInput( dsp, window, StructureNotifyMask ) ;
    XMapWindow( dsp, window );
    return window ;
}

void destroywindow( Display * dsp, Window window )
{
    XDestroyWindow( dsp, window );
}

unsigned int getpixel( struct shmimage * src, struct shmimage * dst,
                       int j, int i, int w, int h )
{
    int x = (float)(i * src->ximage->width) / (float)w ;
    int y = (float)(j * src->ximage->height) / (float)h ;
    return src->data[ y * src->ximage->width + x ] ;
}

int processimage( struct shmimage * src, struct shmimage * dst )
{
    int sw = src->ximage->width ;
    int sh = src->ximage->height ;
    int dw = dst->ximage->width ;
    int dh = dst->ximage->height ;

    // Here you can set the resulting position and size of the captured screen
    // Because of the limitations of this example, it must fit in dst->ximage
    int w = dw / 2 ;
    int h = dh / 2 ;
    int x = ( dw - w ) ;
    int y = ( dh - h ) / 2 ;

    // Just in case...
    if( x < 0 || y < 0 || x + w > dw || y + h > dh || sw < dw || sh < dh )
    {
        printf( NAME   ": This is only a limited example\n" ) ;
        printf( NAMESP "  Please implement a complete scaling algorithm\n" ) ;
        return false ;
    }

    unsigned int * d = dst->data + y * dw + x ;
    int r = dw - w ;
    int j, i ;
    for( j = 0 ; j < h ; ++j )
    {
        for( i = 0 ; i < w ; ++i )
        {
            *d++ = getpixel( src, dst, j, i, w, h ) ;
        }
        d += r ;
    }
    return true ;
}

int run( Display * dsp, Window window, struct shmimage * src, struct shmimage * dst )
{
    XGCValues xgcvalues ;
    xgcvalues.graphics_exposures = False ;
    GC gc = XCreateGC( dsp, window, GCGraphicsExposures, &xgcvalues ) ;

    Atom delete_atom = XInternAtom( dsp, "WM_DELETE_WINDOW", False ) ;
    XSetWMProtocols( dsp, window, &delete_atom, True ) ;

    XEvent xevent ;
    int running = true ;
    int initialized = false ;
    int dstwidth = dst->ximage->width ;
    int dstheight = dst->ximage->height ;
    long framets = timestamp( ) ;
    long periodts = timestamp( ) ;
    long frames = 0 ;
    int fd = ConnectionNumber( dsp ) ;
    while( running )
    {
        while( XPending( dsp ) )
        {
            XNextEvent( dsp, &xevent ) ;
            if( ( xevent.type == ClientMessage && xevent.xclient.data.l[0] == delete_atom )
                || xevent.type == DestroyNotify )
            {
                running = false ;
                break ;
            }
            else if( xevent.type == ConfigureNotify )
            {
                if( xevent.xconfigure.width == dstwidth
                    && xevent.xconfigure.height == dstheight )
                {
                    initialized = true ;
                }
            }
        }
        if( initialized )
        {
            getrootwindow( dsp, src ) ;
            if( !processimage( src, dst ) )
            {
                return false ;
            }
            XShmPutImage( dsp, window, gc, dst->ximage,
                          0, 0, 0, 0, dstwidth, dstheight, False ) ;
            XSync( dsp, False ) ;

            int frameus = timestamp( ) - framets ;
            ++frames ;
            while( frameus < FRAME )
            {
                #if defined( __SLEEP__ )
                usleep( FRAME - frameus ) ;
                #endif
                frameus = timestamp( ) - framets ;
            }
            framets = timestamp( ) ;

            int periodus = timestamp( ) - periodts ;
            if( periodus >= PERIOD )
            {
                printf( "fps: %d\n", (int)round( 1000000.0L * frames / periodus ) ) ;
                frames = 0 ;
                periodts = framets ;
            }
        }
    }
    return true ;
}

int main( int argc, char * argv[] )
{
    Display * dsp = XOpenDisplay( NULL ) ;
    if( !dsp )
    {
        printf( NAME ": could not open a connection to the X server\n" ) ;
        return 1 ;
    }

    if( !XShmQueryExtension( dsp ) )
    {
        XCloseDisplay( dsp ) ;
        printf( NAME ": the X server does not support the XSHM extension\n" ) ;
        return 1 ;
    }

    int screen = XDefaultScreen( dsp ) ;
    struct shmimage src, dst ;
    initimage( &src ) ;
    int width = XDisplayWidth( dsp, screen ) ;
    int height = XDisplayHeight( dsp, screen ) ;
    if( !createimage( dsp, &src, width, height ) )
    {
        XCloseDisplay( dsp ) ;
        return 1 ;
    }
    initimage( &dst ) ;
    int dstwidth = width / 2 ;
    int dstheight = height / 2 ;
    if( !createimage( dsp, &dst, dstwidth, dstheight ) )
    {
        destroyimage( dsp, &src ) ;
        XCloseDisplay( dsp ) ;
        return 1 ;
    }

    if( dst.ximage->bits_per_pixel != 32 )
    {
        destroyimage( dsp, &src ) ;
        destroyimage( dsp, &dst ) ;
        XCloseDisplay( dsp ) ;
        printf( NAME   ": This is only a limited example\n" ) ;
        printf( NAMESP "  Please add support for all pixel formats using: \n" ) ;
        printf( NAMESP "      dst.ximage->bits_per_pixel\n" ) ;
        printf( NAMESP "      dst.ximage->red_mask\n" ) ;
        printf( NAMESP "      dst.ximage->green_mask\n" ) ;
        printf( NAMESP "      dst.ximage->blue_mask\n" ) ;
        return 1 ;
    }

    Window window = createwindow( dsp, dstwidth, dstheight ) ;
    run( dsp, window, &src, &dst ) ;
    destroywindow( dsp, window ) ;  

    destroyimage( dsp, &src ) ;
    destroyimage( dsp, &dst ) ;
    XCloseDisplay( dsp ) ;
    return 0 ;
}
