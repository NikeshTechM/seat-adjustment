FROM scratch

COPY seat_adjustment /seat_adjustment

EXPOSE 60001 44821

ENTRYPOINT ["/seat_adjustment"]
