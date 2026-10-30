import QtQuick
import Artilody.Backend

// GPX Ride: ride a GPX route with the same physics, trainer control and
// controls as the Video Ride (VideoRidePage), shown in 3D instead of a video.
// Session "Gpx" (RlvSession, Kind::Gpx), library "GpxRoutes" (.gpx files).
VideoRidePage {
    gpx: true
    session: Gpx
    library: GpxRoutes
}
