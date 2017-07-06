'use strict';

var suite = require('../../../mapbox-gl-js/test/integration').expression;
var mbgl = require('../index');

var tests;

if (process.argv[1] === __filename && process.argv.length > 2) {
    tests = process.argv.slice(2);
}

suite.run('native', {tests: tests}, (fixture) => {
    const compileResult = {};
    const testResult = {
        compileResult
    };

    const expression = mbgl.Expression.parse(fixture.expression);

    if (expression instanceof mbgl.Expression) {
        compileResult.result = 'success';
        compileResult.isFeatureConstant = expression.isFeatureConstant();
        compileResult.isZoomConstant = expression.isZoomConstant();
        compileResult.type = expression.getType();
        if (fixture.evaluate) {
            const evaluateResults = [];
            for (const input of fixture.evaluate) {
                const zoom = typeof input[0].zoom === 'number' ?
                    input[0].zoom : -1;

                const feature = Object.assign({
                    type: 'Feature',
                    properties: {},
                    geometry: { type: 'Point', coordinates: [0, 0] }
                }, input[1])

                const output = expression.evaluate(zoom, feature);
                evaluateResults.push(output);
            }

            if (evaluateResults.length) {
                testResult.evaluateResults = evaluateResults;
            }
        }
    } else {
        compileResult.result = 'error';
        compileResult.errors = expression;
    }

    return testResult;
});

