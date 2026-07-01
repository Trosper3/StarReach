#pragma once

struct AttributeSet {
    float hull        = 0.0f;
    float shield      = 0.0f;
    float thrust      = 0.0f;
    float damageBonus = 0.0f;

    AttributeSet operator+(const AttributeSet& other) const {
        return { hull + other.hull, shield + other.shield,
                 thrust + other.thrust, damageBonus + other.damageBonus };
    }
};
