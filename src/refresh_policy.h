#pragma once
class RefreshPolicy {
    unsigned int failures=0;
public:
    unsigned long after(bool success) {
        if(success) {failures=0;return 300000;}
        if(failures<3) ++failures;
        return failures==1?5000:failures==2?15000:60000;
    }
};
