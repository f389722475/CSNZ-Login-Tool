#include "frost_policy.hpp"

namespace csnz {
std::optional<CylinderContact> sweptCylinderContact(Vec3 a,Vec3 b,Vec3 lo,Vec3 hi,double radius,double height){
    if(!finite(a)||!finite(b)||!finite(lo)||!finite(hi)||!std::isfinite(radius)||!std::isfinite(height)||radius<0||height<0||lo.x>hi.x||lo.y>hi.y||lo.z>hi.z)throw std::runtime_error("Orb geometry");
    const std::array<double,3> d{double(b.x)-a.x,double(b.y)-a.y,double(b.z)-a.z};const auto half=height*.5;double left=0,right=1;
    if(std::abs(d[2])<1e-12){if(a.z<lo.z-half||a.z>hi.z+half)return {};}
    else{auto x=(lo.z-half-a.z)/d[2],y=(hi.z+half-a.z)/d[2];if(x>y)std::swap(x,y);left=std::max(left,x);right=std::min(right,y);if(left>right)return {};}
    std::vector<double> cuts{left,right};for(unsigned i=0;i<2;i++)if(std::abs(d[i])>1e-12)for(double edge:{double(lo[i]),double(hi[i])}){const auto t=(edge-a[i])/d[i];if(t>left&&t<right)cuts.push_back(t);}std::sort(cuts.begin(),cuts.end());
    std::optional<double> best;double at=left;const auto evaluate=[&](double t){double distance=0;for(unsigned i=0;i<2;i++){const auto p=a[i]+d[i]*t,q=p-clamp(p,lo[i],hi[i]);distance+=q*q;}if(!best||distance<*best){best=distance;at=t;}};
    for(const auto t:cuts)evaluate(t);for(std::size_t k=0;k+1<cuts.size();k++){const auto l=cuts[k],r=cuts[k+1],mid=(l+r)*.5;double A=0,B=0;for(unsigned i=0;i<2;i++){const auto p=a[i]+d[i]*mid;if(p>=lo[i]&&p<=hi[i])continue;const auto edge=p<lo[i]?lo[i]:hi[i];A+=d[i]*d[i];B+=d[i]*(a[i]-edge);}if(A>0)evaluate(clamp(-B/A,l,r));}
    if(best&&*best<=radius*radius+1e-8)return CylinderContact{at,{float(a.x+d[0]*at),float(a.y+d[1]*at),float(a.z+d[2]*at)}};return {};
}
std::array<unsigned char,6> shieldGaugePayload(int entity,double remaining){if(entity<1||entity>32||!std::isfinite(remaining)||remaining<0||remaining>4.01)throw std::runtime_error("Frost gauge payload");std::array<unsigned char,6> out{2,static_cast<unsigned char>(entity),0,0,0,0};const auto f=static_cast<float>(remaining);std::memcpy(out.data()+2,&f,4);return out;}
}
