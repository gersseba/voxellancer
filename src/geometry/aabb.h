#pragma once

#include <list>
#include <glm/glm.hpp>

#include "axis.h"
#include "abstractshape.h"

class Ray;
class Line;
class Sphere;
class Transform;

template<typename T>
class TAABB: public AbstractShape {
public:
    TAABB();
    TAABB(const glm::vec<3, T> &llf, const glm::vec<3, T> &urb);
    template<typename OtherT> TAABB(const TAABB<OtherT>& other);

    inline const glm::vec<3, T> &llf() const;
    inline void setLLF(const glm::vec<3, T> &llf);

    inline const glm::vec<3, T> &urb() const;
    inline void setURB(const glm::vec<3, T> &urb);

    T axisMin(Axis axis) const;
    T axisMax(Axis axis) const;

    glm::vec<3, T> middle() const;

    virtual T extent(Axis axis) const;
    glm::vec<3, T> extent() const;
    T diameter() const;

    TAABB<T> moved(Axis axis, T delta) const;
    TAABB<T> moved(const glm::vec<3, T> &delta) const;

    void move(Axis axis, T delta);
    void move(const glm::vec<3, T> &delta);

    void expand(Axis axis, T delta);
    TAABB<T> expanded(Axis axis, T delta) const;

    template<typename OtherT> bool intersects(const TAABB<OtherT> &other) const;

    bool contains(const TAABB<T> &other) const;
    template<typename OtherT> bool contains(const glm::vec<3, OtherT> &vec) const;

    virtual bool intersects(const Sphere& sphere) const override;
    virtual bool nearTo(const TAABB<int>& other) const override;
    virtual bool containedBy(const TAABB<int>& other) const override;

    TAABB<T> united(const TAABB<T> &other) const;
    void unite(const TAABB<T> &other);

    std::list<TAABB<T>> split(Axis axis) const;
    void split(TAABB<T> &a, TAABB<T> &b, Axis axis) const;
    std::list<TAABB<T>> recursiveSplit(int recursions, Axis axis) const;

    bool operator==(const TAABB<T> &other) const;

    void extend(const glm::vec<3, T> & point);

    static TAABB<float> containing(const Sphere &sphere);


protected:
    glm::vec<3, T> m_llf, m_urb;
};

typedef TAABB<float> AABB;
typedef TAABB<int> IAABB;
typedef TAABB<unsigned char> CAABB;


#include "aabb.inl"
