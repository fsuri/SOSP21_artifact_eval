#ifndef _PBFT_APP_H_
#define _PBFT_APP_H_

#include <string>

namespace pbftstore {

class App {
public:

    App();
    virtual ~App();

    // upcall to execute the message
    virtual void Execute(std::string& type, std::string& msg);
};

}

#endif /* _PBFT_APP_H_ */
