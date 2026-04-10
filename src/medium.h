//
// Created by xuelangyun on 2026/3/31.
//

#ifndef GDEXTENSION_CPP_EXAMPLE_MEDIUM_H
#define GDEXTENSION_CPP_EXAMPLE_MEDIUM_H


namespace godot
{
    class Medium
    {
    };

    struct MediumInterface {
        // MediumInterface Public Methods
        std::string ToString() const;

        MediumInterface() = default;
        
        MediumInterface(Medium medium) : inside(medium), outside(medium) {}
        
        MediumInterface(Medium inside, Medium outside) : inside(inside), outside(outside) {}

        
        //bool IsMediumTransition() const { return inside != outside; }

        // MediumInterface Public Members
        Medium inside, outside;
    };
}

#endif //GDEXTENSION_CPP_EXAMPLE_MEDIUM_H