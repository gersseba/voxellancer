#pragma once

#include <SFML/Network/Socket.hpp>

#include "localstream.h"

inline LocalStream::Status localStreamStatus(sf::Socket::Status status) {
    switch (status) {
    case sf::Socket::Status::Done:
    case sf::Socket::Status::Partial:
        return LocalStream::Status::Done;
    case sf::Socket::Status::NotReady:
        return LocalStream::Status::NotReady;
    case sf::Socket::Status::Disconnected:
        return LocalStream::Status::Disconnected;
    case sf::Socket::Status::Error:
        return LocalStream::Status::Error;
    }
    return LocalStream::Status::Error;
}
