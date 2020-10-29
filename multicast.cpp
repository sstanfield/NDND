//
// Created by sstanf on 10/29/20.
//

#include "multicast.h"

#include <iostream>

using namespace std;

namespace ndn {
    namespace ndnd {

        // XXX bad
        const Name SERVER_DISCOVERY_PREFIX("/ndn/nd/arrival");
        const uint64_t SERVER_DISCOVERY_ROUTE_COST(0);
        const time::milliseconds SERVER_DISCOVERY_ROUTE_EXPIRATION = 30_s;

        MulticastInterest::MulticastInterest(Face &face,
                                             std::shared_ptr <nfd::Controller> controller)
                : m_face(face), m_controller(controller)
        {
            m_ready = false;
            m_error = false;
            nfd::FaceQueryFilter filter;
            filter.setLinkType(nfd::LINK_TYPE_MULTI_ACCESS);

            m_controller->fetch<nfd::FaceQueryDataset>(
                    filter,
                    bind(&MulticastInterest::registerMultiPrefix, this, _1),
                    [this](uint32_t code, const std::string &reason) {
                        cout << "AHND (Multicast): Error " << to_string(code)
                             << " when querying multi-access faces: "
                             << reason << endl;
                        m_error = true;
                    });
        }

        void MulticastInterest::expressInterest(const Interest interest,
                                                const DataCallback &afterSatisfied,
                                                const NackCallback &afterNacked,
                                                const TimeoutCallback &afterTimeout) {
            if (m_ready) {
                m_face.expressInterest(interest,
                                     afterSatisfied,
                                     afterNacked,
                                     afterTimeout);
            } else {
                cout << "AHND (Multicast): Error, expressed interest before ready" << endl;
                m_error = true;
            }
        }

        void MulticastInterest::requestReady() {
            m_ready = true;
            m_error = false;
        }

        void MulticastInterest::setStrategy() {
            nfd::ControlParameters parameters;
            parameters.setName(SERVER_DISCOVERY_PREFIX)
                    .setStrategy("/localhost/nfd/strategy/multicast"),

                    m_controller->start<nfd::StrategyChoiceSetCommand>(
                            parameters,
                            bind(&MulticastInterest::requestReady, this),
                            [this](const nfd::ControlResponse &resp) {
                                cout << "AHND (Multicast): Error " << to_string(resp.getCode())
                                     << " when setting multicast strategy: "
                                     << resp.getText() << endl;
                                m_error = true;
                            });
        }

        void MulticastInterest::afterReg(int nRegSuccess) {
            if (nRegSuccess > 0) {
                setStrategy();
            } else {
                cout << "AHND (Multicast): Cannot register hub discovery prefix for any face" << endl;
                m_error = true;
            }
        }

        void MulticastInterest::registerMultiPrefix(const std::vector <nfd::FaceStatus> &dataset) {
            if (dataset.empty()) {
                cout << "AHND (Multicast): No multi-access faces available" << endl;
                m_error = true;
            }

            int nRegs = dataset.size();
            std::shared_ptr<int> nRegSuccess = std::make_shared<int>(0);
            std::shared_ptr<int> nRegFailure = std::make_shared<int>(0);

            for (const auto &faceStatus : dataset) {
                nfd::ControlParameters parameters;
                parameters.setName(SERVER_DISCOVERY_PREFIX)
                        .setFaceId(faceStatus.getFaceId())
                        .setCost(SERVER_DISCOVERY_ROUTE_COST)
                        .setExpirationPeriod(SERVER_DISCOVERY_ROUTE_EXPIRATION);

                m_controller->start<nfd::RibRegisterCommand>(
                        parameters,
                        [this, nRegSuccess, nRegFailure, nRegs]
                        (const nfd::ControlParameters &) {
                            *nRegSuccess += 1;
                            if (*nRegSuccess + *nRegFailure == nRegs) {
                                afterReg(*nRegSuccess);
                            }
                        },
                        [this, nRegSuccess, nRegFailure, nRegs, faceStatus]
                        (const nfd::ControlResponse &resp) {
                            std::cerr << "AHND (Multicast): Error " << resp.getCode()
                                      << " when registering hub discovery prefix "
                                      << "for face " << faceStatus.getFaceId()
                                      << " (" << faceStatus.getRemoteUri()
                                      << "): " << resp.getText() << std::endl;
                            *nRegFailure += 1;
                            if (*nRegSuccess + *nRegFailure == nRegs) {
                                afterReg(*nRegSuccess);
                            }
                        });
            }
        }
    }
}