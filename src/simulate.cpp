#include <iostream>
#include <iomanip>          // std::setprecision
#include <random>
#include <ctime>
#include <chrono>
#include <thread>

int main(){
    using namespace std;
    uint32_t width{2592}, height{1944};
    double charge_mean{500}, charge_sigma{45};
    double tau_size{0.4};
    double tau_wait{50};

    // Soporte para generador de números aleatorios.
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    default_random_engine generator(seed);
    normal_distribution<double> r_charge(charge_mean,charge_sigma);
    uniform_real_distribution<double> r_width(0,width);
    uniform_real_distribution<double> r_height(0,height);
    exponential_distribution<double> r_size(tau_size);
    exponential_distribution<double> r_wait(tau_wait);
    
    double t = 0;
    for(;;) {
        double wait = r_wait(generator);

        //cerr << "Esperando " <<  setprecision(2) << fixed << wait << " segundos" << endl;
        this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(wait*1000)));
        t += wait;

        int size = r_size(generator)+1;
        cout    << setprecision(2) << fixed << t << '\t'
                << size << '\t' << 0 << '\t'
                << static_cast<uint32_t>(r_charge(generator))*size << '\t'
                << r_width(generator) << '\t' << r_height(generator) << endl;
    }
    
}