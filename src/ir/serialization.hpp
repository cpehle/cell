#pragma once

#include "ir/namespace.h"
#include "ir/serialize_shared_ptr.h"

#include <boost/serialization/map.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/nvp.hpp>

namespace boost {
  namespace serialization {


    template<typename Archive>
    void serialize(Archive& ar, ir::Type& type, unsigned int const version) {
      ar & BOOST_SERIALIZATION_NVP(type.name);
    }


    template<typename Archive>
    void serialize(Archive& ar, ir::Object& obj, unsigned int const version) {
      ar & BOOST_SERIALIZATION_NVP(obj.type);
      ar & BOOST_SERIALIZATION_NVP(obj.name);
    }

    template<typename Archive>
    void serialize(Archive& ar, ir::Port_assignment& assign, unsigned int const version) {
      ar & BOOST_SERIALIZATION_NVP(assign.port);
      ar & BOOST_SERIALIZATION_NVP(assign.object);
    }

    template<typename Archive>
    void serialize(Archive& ar, ir::Instantiation& inst, unsigned int const version) {
      ar & BOOST_SERIALIZATION_NVP(inst.name);
      ar & BOOST_SERIALIZATION_NVP(inst.module);
      ar & BOOST_SERIALIZATION_NVP(inst.connection);
    }

    template<typename Archive>
    void serialize(Archive& ar, ir::Function& func, unsigned int const version) {
      ar & BOOST_SERIALIZATION_NVP(func.return_type);
      ar & BOOST_SERIALIZATION_NVP(func.name);
      ar & BOOST_SERIALIZATION_NVP(func.parameters);
    }

    template<typename Archive>
    void serialize(Archive& ar, ir::Port& port, unsigned int const version) {
      ar & BOOST_SERIALIZATION_NVP(port.name);
      ar & BOOST_SERIALIZATION_NVP(port.direction);
      ar & BOOST_SERIALIZATION_NVP(port.type);
    }

    template<typename Archive>
    void serialize(Archive& ar, ir::Socket& sock, unsigned int const version) {
      ar & BOOST_SERIALIZATION_NVP(sock.name);
      ar & BOOST_SERIALIZATION_NVP(sock.ports);
    }

    template<typename Archive>
    void serialize(Archive& ar, ir::Module& mod, unsigned int const version) {
      ar & boost::serialization::make_nvp("Namespace",
          boost::serialization::base_object<ir::Namespace>(mod));
      ar & BOOST_SERIALIZATION_NVP(mod.name);
      ar & BOOST_SERIALIZATION_NVP(mod.socket);
      ar & BOOST_SERIALIZATION_NVP(mod.objects);
      ar & BOOST_SERIALIZATION_NVP(mod.instantiations);
    }

    template<typename Archive>
    void serialize(Archive& ar, ir::Namespace& ns, unsigned int const version) {
      ar & BOOST_SERIALIZATION_NVP(ns.name);
      ar & BOOST_SERIALIZATION_NVP(ns.modules);
      ar & BOOST_SERIALIZATION_NVP(ns.namespaces);
      ar & BOOST_SERIALIZATION_NVP(ns.types);
      ar & BOOST_SERIALIZATION_NVP(ns.functions);
      ar & BOOST_SERIALIZATION_NVP(ns.sockets);
    }


  }
}

/* vim: set et ff=unix sts=2 sw=2 ts=2 : */
