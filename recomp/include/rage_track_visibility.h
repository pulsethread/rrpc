#pragma once
// Homogeneous frustum tests retain triangles crossing the eye or a viewport
// edge. A shared outside half-space is the only reason to reject a triangle.
struct RageTrackProjection {double x=0,y=0,z=0;};
inline unsigned rage_track_outcode(const RageTrackProjection& p,int left,int top,int right,int bottom,float far_limit){
    return (p.z<1?1u:0u)|(p.z>far_limit?2u:0u)|
        (p.x<left*p.z?4u:0u)|(p.x>right*p.z?8u:0u)|
        (p.y<top*p.z?16u:0u)|(p.y>bottom*p.z?32u:0u);
}
