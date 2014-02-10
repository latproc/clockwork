
CountSimulator MACHINE {
    OPTION VALUE 0;
    OPTION rate 12;

    updating WHEN SELF IS idle AND TIMER > 10;
    idle DEFAULT;
    ENTER updating { VALUE := VALUE + rate }
}

RateEstimatorSettings MACHINE {
    OPTION rate 12;
}

count_sim CountSimulator(rate:11);
estimator_settings RateEstimatorSettings;
rate RATEESTIMATOR count_sim, estimator_settings;
