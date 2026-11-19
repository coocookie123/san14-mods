#include "../src/office_policy.h"
#include <cassert>
#include <iostream>

using namespace companion_office_policy;
int main(){
    const Officer high{10,90,85,70,50,20000},strategist{20,82,40,85,75,10000};
    const Officer premier{30,65,50,90,95,15000},tie_a{40,60,40,80,85,8000},tie_b{50,60,40,80,85,8000};
    assert(military_class(high)==1&&military_class(strategist)==1&&military_class(premier)==0);
    assert(score(high,20000,Military)==185000000);
    assert(score(premier,20000,Civil)==165000000);
    assert((rank({tie_b,tie_a,premier},Civil)==std::vector<size_t>{2,1,0}));
    assert((rank({strategist,high},Military)==std::vector<size_t>{1,0}));
    assert((rank({tie_a,strategist,premier,high},Comprehensive)==std::vector<size_t>{3,2,1,0}));
    // Strict > threshold: equality is still civil; stats are capped individually.
    assert(military_class({1,70,70,90,0,0})==0);
    assert(military_class({1,70,71,90,0,0})==1);
    assert(military_class({1,70,0,91,0,0})==1);
    assert(military_class({1,250,40,60,0,0})==1);
    const Officer above_100{61,110,110,110,110,8000},above_150{62,150,150,150,150,8000};
    assert((rank({above_100,above_150},Military)==std::vector<size_t>{1,0}));
    assert((rank({above_100,above_150},Civil)==std::vector<size_t>{1,0}));
    assert(stat(-1)==0&&stat(255)==255&&stat(256)==255);
    const Officer maximum{63,255,255,255,255,32767};
    assert(score(maximum,32767,Military)==INT64_C(556219825));
    assert(military_class({0,100,100,100,100,100})==-1);
    assert(military_class({6001,100,100,100,100,100})==-1);
    assert(score({0,100,100,100,100,100},100,Military)==-1);
    auto negative=premier;negative.merit=-1000;auto zero=negative;zero.merit=0;
    assert(score(negative,0,Civil)==score(zero,1,Civil));
    assert(maximum_merit({negative,zero})==1);
    auto capped=high;capped.merit=32767;
    assert(score(capped,32767,Military)==INT64_C(303094750));
    assert((rank({{},high,{},high},Comprehensive)==std::vector<size_t>{1,3,0,2}));
    assert(rank({},Military).empty());
    // Post classification preserves every native bonus whenever any is positive.
    const int32_t none[4]={0,0,0,0},positive[4]={0,2,0,0};
    for(uint32_t i=0;i<4;++i){
        assert(classify_post(1,i,none)==1&&classify_post(5,i,none)==1);
        assert(classify_post(2,i,none)==(i>=2?1:0));
        assert(classify_post(6,i,none)==(i<2?1:0));
        assert(classify_post(6,i,positive)==positive[i]);
        assert(classify_post(73,i,none)==0);
    }
    std::cout<<"Companion office policy: original scores/order, strict classification, stable ties and post bonuses passed\n";
}
