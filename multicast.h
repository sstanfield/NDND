//
// Created by sstanf on 10/29/20.
//

#ifndef NDND_MULTICAST_H
#define NDND_MULTICAST_H

#include <ndn-cxx/face.hpp>
#include <ndn-cxx/mgmt/nfd/controller.hpp>

namespace ndn {
    namespace ndnd {

        class MulticastInterest {
        private:
            Face &m_face;
            std::shared_ptr<nfd::Controller> m_controller;
            bool m_ready;
            bool m_error;

            void requestReady();
            void setStrategy();
            void afterReg(int nRegSuccess);
            void registerMultiPrefix(const std::vector <nfd::FaceStatus> &dataset);

        public:
            MulticastInterest(Face &face,
                              std::shared_ptr <nfd::Controller> controller);
            bool is_ready() { return m_ready; }
            bool is_error() { return m_error; }
            void expressInterest(const Interest interest,
                                 const DataCallback &afterSatisfied,
                                 const NackCallback &afterNacked,
                                 const TimeoutCallback &afterTimeout);
        };
    }
}

#endif //NDND_MULTICAST_H
