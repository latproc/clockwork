
CountSimulator MACHINE {
    OPTION VALUE 0;
    OPTION rate 12;

    run FLAG;
    waiting WHEN run IS off;
    updating WHEN SELF IS idle AND TIMER >= 10000;
    idle DEFAULT;
    ENTER updating { VALUE := VALUE + rate }
}

RateEstimatorSettings MACHINE {
    OPTION rate 12;
}

count_sim CountSimulator(rate:11);
estimator_settings RateEstimatorSettings;
rate RATEESTIMATOR count_sim, estimator_settings;
